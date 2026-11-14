// Copyright 2026 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <grpc/grpc.h>
#include <grpc/slice.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "src/core/credentials/transport/security_connector.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/security/security_handshaker.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/transport/auth_context.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/util/unique_type_name.h"
#include "test/core/test_util/mock_endpoint.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {
using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::MockEndpointController;

std::string Bytes(const unsigned char* bytes, size_t size) {
  return size == 0 ? std::string()
                   : std::string(reinterpret_cast<const char*>(bytes), size);
}

std::string Payload(size_t size) {
  std::string value(size, '\0');
  for (size_t i = 0; i < size; ++i)
    value[i] = static_cast<char>((i + 37) % 251);
  return value;
}

struct Step {
  std::string expected;
  tsi_result status = TSI_FAILED_PRECONDITION;
  bool asynchronous = false;
  bool finish = false;
  std::string output;
  std::vector<std::string> next_read;
};

struct Script {
  std::mutex mu;
  std::condition_variable changed;
  std::vector<Step> steps;
  std::vector<std::string> observed;
  std::shared_ptr<MockEndpointController> endpoint;
  HandshakerArgs* args = nullptr;
  const unsigned char* pending_bytes = nullptr;
  size_t pending_size = 0;
  size_t pending_step = 0;
  tsi_handshaker_on_next_done_cb pending_cb = nullptr;
  void* pending_user_data = nullptr;
  bool initial_next_returned = false;
  std::atomic<int> shutdowns{0};
  std::atomic<int> destructions{0};
  std::atomic<int> result_destructions{0};
  std::atomic<int> completions{0};
  std::atomic<int> peer_checks{0};
  std::atomic<int> source_releases{0};
  absl::Status completion_status;
  std::string unused;
};

struct ScriptResult : tsi_handshaker_result {
  explicit ScriptResult(std::shared_ptr<Script> script)
      : script(std::move(script)), unused("tail\0bytes", 10) {
    vtable = &kVtable;
  }
  static tsi_result ExtractPeer(const tsi_handshaker_result*, tsi_peer* peer) {
    return tsi_construct_peer(0, peer);
  }
  static tsi_result ProtectorType(const tsi_handshaker_result*,
                                  tsi_frame_protector_type* type) {
    *type = TSI_FRAME_PROTECTOR_NONE;
    return TSI_OK;
  }
  static tsi_result Unused(const tsi_handshaker_result* base,
                           const unsigned char** bytes, size_t* size) {
    auto* self = static_cast<const ScriptResult*>(base);
    *bytes = reinterpret_cast<const unsigned char*>(self->unused.data());
    *size = self->unused.size();
    return TSI_OK;
  }
  static void Destroy(tsi_handshaker_result* base) {
    auto* self = static_cast<ScriptResult*>(base);
    auto script = self->script;
    delete self;
    std::lock_guard<std::mutex> lock(script->mu);
    ++script->result_destructions;
    script->changed.notify_all();
  }
  static const tsi_handshaker_result_vtable kVtable;
  std::shared_ptr<Script> script;
  std::string unused;
};
const tsi_handshaker_result_vtable ScriptResult::kVtable = {
    ExtractPeer, ProtectorType, nullptr, nullptr, Unused, Destroy};

struct ScriptTsi : tsi_handshaker {
  explicit ScriptTsi(std::shared_ptr<Script> script)
      : tsi_handshaker(&kVtable), script(std::move(script)) {}
  static tsi_result Next(tsi_handshaker* base, const unsigned char* bytes,
                         size_t size, const unsigned char** output,
                         size_t* output_size, tsi_handshaker_result** result,
                         tsi_handshaker_on_next_done_cb cb, void* user_data,
                         std::string*) {
    auto* self = static_cast<ScriptTsi*>(base);
    auto& script = *self->script;
    std::lock_guard<std::mutex> lock(script.mu);
    const size_t index = script.observed.size();
    EXPECT_LT(index, script.steps.size());
    if (index >= script.steps.size()) return TSI_INTERNAL_ERROR;
    const Step& step = script.steps[index];
    script.observed.push_back(Bytes(bytes, size));
    EXPECT_EQ(script.observed.back(), step.expected);
    EXPECT_EQ(script.args->read_buffer.Length(), 0u);
    EXPECT_EQ(script.args->read_buffer.Count(), 0u);
    for (const auto& fragment : step.next_read) {
      script.endpoint->TriggerReadEvent(
          grpc_event_engine::experimental::Slice::FromCopiedBuffer(
              fragment.data(), fragment.size()));
    }
    if (step.asynchronous) {
      EXPECT_EQ(script.pending_cb, nullptr);
      script.pending_bytes = bytes;  // Deliberately borrowed until callback.
      script.pending_size = size;
      script.pending_step = index;
      script.pending_cb = cb;
      script.pending_user_data = user_data;
      script.changed.notify_all();
      return TSI_ASYNC;
    }
    *output = reinterpret_cast<const unsigned char*>(step.output.data());
    *output_size = step.output.size();
    *result = step.finish ? new ScriptResult(self->script) : nullptr;
    self->handshaker_result_created = step.finish;
    script.changed.notify_all();
    return step.status;
  }
  static void Shutdown(tsi_handshaker* base, bool) {
    auto script = static_cast<ScriptTsi*>(base)->script;
    std::lock_guard<std::mutex> lock(script->mu);
    ++script->shutdowns;
    script->changed.notify_all();
    // Cancellation completion is deliberately delayed by the test.
  }
  static void Destroy(tsi_handshaker* base) {
    auto* self = static_cast<ScriptTsi*>(base);
    auto script = self->script;
    delete self;
    std::lock_guard<std::mutex> lock(script->mu);
    ++script->destructions;
    script->changed.notify_all();
  }
  static const tsi_handshaker_vtable kVtable;
  std::shared_ptr<Script> script;
};
const tsi_handshaker_vtable ScriptTsi::kVtable = {
    nullptr, nullptr, nullptr, nullptr, nullptr, Destroy, Next, Shutdown};

class ScriptConnector : public grpc_security_connector {
 public:
  explicit ScriptConnector(std::shared_ptr<Script> script)
      : grpc_security_connector("test"), script_(std::move(script)) {}
  void check_peer(tsi_peer peer, grpc_endpoint*, const ChannelArgs&,
                  RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    tsi_peer_destruct(&peer);
    *auth_context = MakeRefCounted<grpc_auth_context>(nullptr);
    {
      std::lock_guard<std::mutex> lock(script_->mu);
      ++script_->peer_checks;
    }
    ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, absl::OkStatus());
  }
  void cancel_check_peer(grpc_closure*, grpc_error_handle) override {}
  int cmp(const grpc_security_connector*) const override { return 0; }
  UniqueTypeName type() const override {
    static UniqueTypeName::Factory factory("scripted-buffer-test");
    return factory.Create();
  }

 private:
  std::shared_ptr<Script> script_;
};

class PreloadHandshaker : public Handshaker {
 public:
  PreloadHandshaker(std::shared_ptr<Script> script, std::vector<Slice> slices)
      : script_(std::move(script)), slices_(std::move(slices)) {}
  absl::string_view name() const override { return "buffer_test_preload"; }
  void DoHandshake(HandshakerArgs* args,
                   absl::AnyInvocable<void(absl::Status)> done) override {
    script_->args = args;
    for (auto& slice : slices_) args->read_buffer.Append(std::move(slice));
    InvokeOnHandshakeDone(args, std::move(done), absl::OkStatus());
  }
  void Shutdown(absl::Status) override {}

 private:
  std::shared_ptr<Script> script_;
  std::vector<Slice> slices_;
};

// Observe the real DoHandshake() boundary without exposing its private class.
// The pending callback must not be resumed until that method has returned;
// otherwise a temporary buffer might accidentally survive long enough to pass.
class ObservedHandshaker : public Handshaker {
 public:
  ObservedHandshaker(RefCountedPtr<Handshaker> inner,
                     std::shared_ptr<Script> script)
      : inner_(std::move(inner)), script_(std::move(script)) {}
  absl::string_view name() const override { return inner_->name(); }
  void DoHandshake(HandshakerArgs* args,
                   absl::AnyInvocable<void(absl::Status)> done) override {
    inner_->DoHandshake(args, std::move(done));
    std::lock_guard<std::mutex> lock(script_->mu);
    script_->initial_next_returned = true;
    script_->changed.notify_all();
  }
  void Shutdown(absl::Status error) override {
    inner_->Shutdown(std::move(error));
  }

 private:
  RefCountedPtr<Handshaker> inner_;
  std::shared_ptr<Script> script_;
};

class SecurityHandshakerBufferTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_init();
    engine_ = grpc_event_engine::experimental::GetDefaultEventEngine();
    script_ = std::make_shared<Script>();
    script_->endpoint = MockEndpointController::Create(engine_);
    connector_ = MakeRefCounted<ScriptConnector>(script_);
    manager_ = MakeRefCounted<HandshakeManager>();
  }
  void Start(std::vector<Slice> slices = {}) {
    ExecCtx exec_ctx;
    ChannelArgs args = ChannelArgs().SetObject<EventEngine>(engine_);
    manager_->Add(
        MakeRefCounted<PreloadHandshaker>(script_, std::move(slices)));
    manager_->Add(MakeRefCounted<ObservedHandshaker>(
        SecurityHandshakerCreate(new ScriptTsi(script_), connector_.get(),
                                 args),
        script_));
    manager_->DoHandshake(
        OrphanablePtr<grpc_endpoint>(script_->endpoint->TakeCEndpoint()), args,
        Timestamp::Now() + Duration::Seconds(30), nullptr,
        [script = script_](absl::StatusOr<HandshakerArgs*> result) {
          std::lock_guard<std::mutex> lock(script->mu);
          ++script->completions;
          script->completion_status = result.status();
          if (result.ok()) {
            script->unused = (*result)->read_buffer.JoinIntoString();
            (*result)->endpoint.reset();
          }
          script->changed.notify_all();
        });
  }
  template <typename Predicate>
  bool Await(Predicate predicate) {
    std::unique_lock<std::mutex> lock(script_->mu);
    return script_->changed.wait_for(lock, std::chrono::seconds(10), predicate);
  }
  void Resume() {
    tsi_handshaker_on_next_done_cb cb;
    void* user_data;
    Step step;
    {
      std::lock_guard<std::mutex> lock(script_->mu);
      ASSERT_NE(script_->pending_cb, nullptr);
      step = script_->steps[script_->pending_step];
      // Access after next() returned, and also after shutdown in that test.
      EXPECT_EQ(Bytes(script_->pending_bytes, script_->pending_size),
                step.expected);
      cb = std::exchange(script_->pending_cb, nullptr);
      user_data = script_->pending_user_data;
    }
    const auto caller = std::this_thread::get_id();
    std::thread callback_thread([script = script_, cb, user_data, step,
                                 caller] {
      EXPECT_NE(std::this_thread::get_id(), caller);
      ExecCtx exec_ctx;
      cb(step.status, user_data,
         reinterpret_cast<const unsigned char*>(step.output.data()),
         step.output.size(), step.finish ? new ScriptResult(script) : nullptr);
    });
    callback_thread.join();
  }
  void TearDown() override {
    if (manager_ != nullptr) {
      ExecCtx exec_ctx;
      manager_->Shutdown(absl::CancelledError("test cleanup"));
    }
    bool pending;
    {
      std::lock_guard<std::mutex> lock(script_->mu);
      pending = script_->pending_cb != nullptr;
    }
    if (pending) Resume();
    EXPECT_TRUE(Await([&] { return script_->completions == 1; }));
    {
      ExecCtx exec_ctx;
      manager_.reset();
      connector_.reset();
      script_->endpoint.reset();
    }
    EXPECT_TRUE(Await([&] { return script_->destructions == 1; }));
    engine_.reset();
    grpc_shutdown_blocking();
  }
  std::shared_ptr<Script> script_;
  std::shared_ptr<EventEngine> engine_;
  RefCountedPtr<ScriptConnector> connector_;
  RefCountedPtr<HandshakeManager> manager_;
};

class InputSizesTest
    : public SecurityHandshakerBufferTest,
      public ::testing::WithParamInterface<std::tuple<int, bool>> {};

TEST_P(InputSizesTest, ReadaheadPreservesExactBinaryInput) {
  const auto [size, fragmented] = GetParam();
  const std::string input = Payload(size);
  script_->steps = {{input}};
  std::vector<Slice> slices;
  const size_t split = fragmented ? input.size() / 2 : input.size();
  slices.push_back(Slice::FromCopiedBuffer(input.data(), split));
  if (fragmented) {
    slices.push_back(
        Slice::FromCopiedBuffer(input.data() + split, input.size() - split));
  }
  Start(std::move(slices));
  ASSERT_TRUE(Await([&] { return script_->completions == 1; }));
  EXPECT_FALSE(script_->completion_status.ok());
  EXPECT_EQ(script_->observed, std::vector<std::string>({input}));
}
INSTANTIATE_TEST_SUITE_P(Boundaries, InputSizesTest,
                         ::testing::Combine(::testing::Values(0, 1, 255, 256,
                                                              257, 4096),
                                            ::testing::Bool()));

TEST_F(SecurityHandshakerBufferTest, FragmentedRepeatedReadsGrowThenShrink) {
  const std::string large = Payload(4097);
  script_->steps = {
      {"", TSI_INCOMPLETE_DATA, false, false, "", {"a", "b", "c"}},
      {"abc", TSI_OK, false, false, "outgoing-flight", {}},
      {large, TSI_INCOMPLETE_DATA, false, false, "", {"z"}},
      {"z", TSI_OK, false, true}};
  // The write completes asynchronously before the next read. Queue that
  // read's fragments during its preceding TSI call, before Read() is issued.
  script_->steps[1].next_read = {large.substr(0, 2039), large.substr(2039)};
  Start();
  ASSERT_TRUE(Await([&] { return script_->completions == 1; }));
  EXPECT_TRUE(script_->completion_status.ok());
  EXPECT_EQ(script_->observed,
            std::vector<std::string>({"", "abc", large, "z"}));
  EXPECT_EQ(script_->unused, std::string("tail\0bytes", 10));
  EXPECT_EQ(script_->peer_checks.load(), 1);
  EXPECT_EQ(script_->result_destructions.load(), 1);
}

TEST_P(InputSizesTest, AsyncInputSurvivesUntilCallback) {
  const auto [size, fragmented] = GetParam();
  const std::string input = Payload(size);
  script_->steps = {{input, TSI_OK, true, true}};
  std::vector<Slice> slices;
  const size_t split = fragmented ? input.size() / 2 : input.size();
  slices.push_back(Slice::FromCopiedBuffer(input.data(), split));
  if (fragmented) {
    slices.push_back(
        Slice::FromCopiedBuffer(input.data() + split, input.size() - split));
  }
  Start(std::move(slices));
  ASSERT_TRUE(Await([&] {
    return script_->pending_cb != nullptr && script_->initial_next_returned;
  }));
  EXPECT_EQ(script_->destructions.load(), 0);
  EXPECT_EQ(script_->completions.load(), 0);
  Resume();
  ASSERT_TRUE(Await([&] { return script_->completions == 1; }));
  EXPECT_TRUE(script_->completion_status.ok());
  EXPECT_EQ(script_->unused, std::string("tail\0bytes", 10));
  EXPECT_EQ(script_->result_destructions.load(), 1);
}

TEST_F(SecurityHandshakerBufferTest, BorrowedSingleSliceGetsOwnedStorage) {
  // Keep the static/borrowed source alive for the entire process. Pointer
  // inequality tests the wrapper's ownership guarantee without violating the
  // static-slice lifetime contract or mutating immutable input.
  static const std::string input = Payload(600);
  script_->steps = {{input, TSI_FAILED_PRECONDITION, true}};
  std::vector<Slice> slices;
  slices.push_back(Slice::FromStaticBuffer(input.data(), input.size()));
  Start(std::move(slices));
  ASSERT_TRUE(Await([&] {
    return script_->pending_cb != nullptr && script_->initial_next_returned;
  }));
  EXPECT_NE(script_->pending_bytes,
            reinterpret_cast<const unsigned char*>(input.data()));
  Resume();
  ASSERT_TRUE(Await([&] { return script_->completions == 1; }));
  EXPECT_FALSE(script_->completion_status.ok());
}

TEST_F(SecurityHandshakerBufferTest, ReleasesSingleInputBackingBeforeCallback) {
  struct SourceStorage {
    std::string bytes;
    std::shared_ptr<Script> script;
  };
  auto* source = new SourceStorage{Payload(65536), script_};
  const std::string input = source->bytes.substr(37, 600);
  Slice backing(grpc_slice_new_with_user_data(
      source->bytes.data(), source->bytes.size(),
      [](void* data) {
        auto* storage = static_cast<SourceStorage*>(data);
        ++storage->script->source_releases;
        delete storage;
      },
      source));
  std::vector<Slice> slices;
  slices.push_back(backing.TakeSubSlice(37, input.size()));
  script_->steps = {{input, TSI_OK, true, true}};
  Start(std::move(slices));
  ASSERT_TRUE(Await([&] {
    return script_->pending_cb != nullptr && script_->initial_next_returned;
  }));
  // As in the baseline, release the whole incoming allocation after copying
  // its small sub-slice, while keeping the independent TSI input alive.
  EXPECT_EQ(script_->source_releases.load(), 1);
  Resume();
  ASSERT_TRUE(Await([&] { return script_->completions == 1; }));
  EXPECT_TRUE(script_->completion_status.ok());
  EXPECT_EQ(script_->source_releases.load(), 1);
}

TEST_F(SecurityHandshakerBufferTest,
       ShutdownKeepsAsyncInputAndDestroysResultOnce) {
  const std::string input = Payload(4097);
  script_->steps = {{input, TSI_OK, true, true}};
  std::vector<Slice> slices;
  slices.push_back(Slice::FromCopiedBuffer(input.data(), input.size()));
  Start(std::move(slices));
  ASSERT_TRUE(Await([&] {
    return script_->pending_cb != nullptr && script_->initial_next_returned;
  }));
  {
    ExecCtx exec_ctx;
    manager_->Shutdown(absl::CancelledError("test shutdown before callback"));
    manager_->Shutdown(absl::CancelledError("repeat shutdown"));
    // Drop caller references while TSI completion is still pending.
    manager_.reset();
    connector_.reset();
  }
  EXPECT_EQ(script_->shutdowns.load(), 1);
  EXPECT_EQ(script_->destructions.load(), 0);
  EXPECT_EQ(script_->completions.load(), 0);
  Resume();
  ASSERT_TRUE(Await([&] { return script_->completions == 1; }));
  EXPECT_FALSE(script_->completion_status.ok());
  EXPECT_EQ(script_->peer_checks.load(), 0);
  EXPECT_EQ(script_->result_destructions.load(), 1);
  ASSERT_TRUE(Await([&] { return script_->destructions == 1; }));
}

TEST_F(SecurityHandshakerBufferTest, EndpointReadFailureCompletesOnce) {
  script_->steps = {{"", TSI_INCOMPLETE_DATA}};
  script_->endpoint->NoMoreReads();
  Start();
  ASSERT_TRUE(Await([&] { return script_->completions == 1; }));
  EXPECT_FALSE(script_->completion_status.ok());
  EXPECT_EQ(script_->shutdowns.load(), 1);
  EXPECT_EQ(script_->observed, std::vector<std::string>({""}));
}

TEST_F(SecurityHandshakerBufferTest,
       ShutdownDuringAsyncFragmentedEndpointRead) {
  const std::string input = Payload(4097);
  script_->steps = {{"",
                     TSI_INCOMPLETE_DATA,
                     false,
                     false,
                     "",
                     {input.substr(0, 2048), input.substr(2048)}},
                    {input, TSI_OK, true, true}};
  Start();
  ASSERT_TRUE(Await([&] {
    return script_->pending_cb != nullptr && script_->initial_next_returned;
  }));
  {
    ExecCtx exec_ctx;
    // Shutdown acquires the security handshaker mutex, so the endpoint's
    // OnHandshakeDataReceivedFromPeerFn has left its TSI call before we read
    // the retained pointer in Resume().
    manager_->Shutdown(absl::CancelledError("cancel async endpoint read"));
    manager_.reset();
    connector_.reset();
  }
  EXPECT_EQ(script_->destructions.load(), 0);
  Resume();
  ASSERT_TRUE(Await([&] { return script_->completions == 1; }));
  EXPECT_FALSE(script_->completion_status.ok());
  EXPECT_EQ(script_->observed, std::vector<std::string>({"", input}));
  EXPECT_EQ(script_->shutdowns.load(), 1);
  EXPECT_EQ(script_->peer_checks.load(), 0);
  EXPECT_EQ(script_->result_destructions.load(), 1);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
