//
//
// Copyright 2015 gRPC authors.
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
//
//

#ifndef GRPC_TEST_CORE_END2END_END2END_TESTS_H
#define GRPC_TEST_CORE_END2END_END2END_TESTS_H

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "gtest/gtest.h"

#include <grpc/byte_buffer.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call_test_only.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/test_config.h"

// Test feature flags.
#define FEATURE_MASK_DOES_NOT_SUPPORT_RETRY 1
#define FEATURE_MASK_SUPPORTS_HOSTNAME_VERIFICATION 2
// Feature mask supports call credentials with a minimum security level of
// GRPC_PRIVACY_AND_INTEGRITY.
#define FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS 4
// Feature mask supports call credentials with a minimum security level of
// GRPC_SECURTITY_NONE.
#define FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE 8
#define FEATURE_MASK_SUPPORTS_REQUEST_PROXYING 16
#define FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL 32
#define FEATURE_MASK_IS_HTTP2 64
#define FEATURE_MASK_ENABLES_TRACES 128
#define FEATURE_MASK_1BYTE_AT_A_TIME 256
#define FEATURE_MASK_DOES_NOT_SUPPORT_WRITE_BUFFERING 512
#define FEATURE_MASK_DOES_NOT_SUPPORT_CLIENT_HANDSHAKE_COMPLETE_FIRST 1024
#define FEATURE_MASK_IS_MINSTACK 2048
#define FEATURE_MASK_IS_SECURE 4096

#define FAIL_AUTH_CHECK_SERVER_ARG_NAME "fail_auth_check"

namespace grpc_core {
class CoreTestFixture {
 public:
  virtual ~CoreTestFixture() {
    ShutdownServer();
    ShutdownClient();
    grpc_completion_queue_shutdown(cq());
    DrainCq();
    grpc_completion_queue_destroy(cq());
  }

  grpc_completion_queue* cq() { return cq_; }
  grpc_server* server() { return server_; }
  grpc_channel* client() { return client_; }

  void InitServer(const ChannelArgs& args) {
    if (server_ != nullptr) ShutdownServer();
    server_ = MakeServer(args);
    GPR_ASSERT(server_ != nullptr);
  }
  void InitClient(const ChannelArgs& args) {
    if (client_ != nullptr) ShutdownClient();
    client_ = MakeClient(args);
    GPR_ASSERT(client_ != nullptr);
  }

  void ShutdownServer() {
    if (server_ == nullptr) return;
    grpc_server_shutdown_and_notify(server_, cq_, server_);
    grpc_event ev;
    do {
      ev = grpc_completion_queue_next(cq_, grpc_timeout_seconds_to_deadline(5),
                                      nullptr);
    } while (ev.type != GRPC_OP_COMPLETE || ev.tag != server_);
    DestroyServer();
  }

  void DestroyServer() {
    if (server_ == nullptr) return;
    grpc_server_destroy(server_);
    server_ = nullptr;
  }

  void ShutdownClient() {
    if (client_ == nullptr) return;
    grpc_channel_destroy(client_);
    client_ = nullptr;
  }

  virtual grpc_server* MakeServer(const ChannelArgs& args) = 0;
  virtual grpc_channel* MakeClient(const ChannelArgs& args) = 0;

 protected:
  void SetServer(grpc_server* server);
  void SetClient(grpc_channel* client);

 private:
  void DrainCq() {
    grpc_event ev;
    do {
      ev = grpc_completion_queue_next(cq_, grpc_timeout_seconds_to_deadline(5),
                                      nullptr);
    } while (ev.type != GRPC_QUEUE_SHUTDOWN);
  }

  grpc_completion_queue* cq_ = grpc_completion_queue_create_for_next(nullptr);
  grpc_server* server_ = nullptr;
  grpc_channel* client_ = nullptr;
};

Slice RandomSlice(size_t length);
Slice RandomBinarySlice(size_t length);
using ByteBufferUniquePtr =
    std::unique_ptr<grpc_byte_buffer, void (*)(grpc_byte_buffer*)>;
ByteBufferUniquePtr ByteBufferFromSlice(Slice slice);

struct CoreTestConfiguration {
  // A descriptive name for this test fixture.
  const char* name;

  // Which features are supported by this fixture. See feature flags above.
  uint32_t feature_mask;

  // If the call host is setup by the fixture (for example, via the
  // GRPC_SSL_TARGET_NAME_OVERRIDE_ARG channel arg), which value should the
  // test expect to find in call_details.host
  const char* overridden_call_host;

  std::function<std::unique_ptr<CoreTestFixture>(
      const ChannelArgs& client_args, const ChannelArgs& server_args)>
      create_fixture;
};

// Base class for e2e tests.
// Notes:
// - older compilers fail matching absl::string_view with some gmock matchers on
//   older compilers, and it's tremendously convenient to be able to do so. So
//   we use std::string for return types here - performance isn't particularly
//   important, so an extra copy is fine.
class CoreEnd2endTest
    : public ::testing::TestWithParam<const CoreTestConfiguration*> {
 public:
  void SetUp() override;
  void TearDown() override;

  class Call;

  class ClientCallBuilder {
   public:
    ClientCallBuilder(CoreEnd2endTest& test, std::string method)
        : test_(test),
          call_selector_(UnregisteredCall{std::move(method), absl::nullopt}) {}
    ClientCallBuilder(CoreEnd2endTest& test, void* registered_call)
        : test_(test), call_selector_(registered_call) {}

    ClientCallBuilder& Host(std::string host) {
      absl::get<UnregisteredCall>(call_selector_).host = std::move(host);
      return *this;
    }
    ClientCallBuilder& Timeout(Duration timeout) {
      if (timeout == Duration::Infinity()) {
        deadline_ = gpr_inf_future(GPR_CLOCK_REALTIME);
        return *this;
      }
      deadline_ = grpc_timeout_milliseconds_to_deadline(timeout.millis());
      return *this;
    }
    Call Create();

   private:
    CoreEnd2endTest& test_;
    struct UnregisteredCall {
      std::string method;
      absl::optional<std::string> host;
    };
    absl::variant<void*, UnregisteredCall> call_selector_;
    grpc_call* parent_call_ = nullptr;
    uint32_t propagation_mask_ = GRPC_PROPAGATE_DEFAULTS;
    gpr_timespec deadline_ = gpr_inf_future(GPR_CLOCK_REALTIME);
  };

  class IncomingMetadata {
   public:
    IncomingMetadata() = default;
    ~IncomingMetadata() {
      if (metadata_ != nullptr) grpc_metadata_array_destroy(metadata_.get());
    }

    absl::optional<std::string> Get(absl::string_view key) const;

    grpc_op MakeOp();

   private:
    std::unique_ptr<grpc_metadata_array> metadata_ =
        std::make_unique<grpc_metadata_array>(
            grpc_metadata_array{0, 0, nullptr});
  };

  class IncomingMessage {
   public:
    IncomingMessage() = default;
    IncomingMessage(const IncomingMessage&) = delete;
    IncomingMessage& operator=(const IncomingMessage&) = delete;
    ~IncomingMessage() {
      if (payload_ != nullptr) grpc_byte_buffer_destroy(payload_);
    }

    std::string payload() const;
    bool is_end_of_stream() const { return payload_ == nullptr; }
    grpc_byte_buffer_type byte_buffer_type() const { return payload_->type; }
    grpc_compression_algorithm compression() const {
      return payload_->data.raw.compression;
    }

    grpc_op MakeOp();

   private:
    grpc_byte_buffer* payload_ = nullptr;
  };

  class IncomingStatusOnClient {
   public:
    IncomingStatusOnClient() = default;
    IncomingStatusOnClient(const IncomingStatusOnClient&) = delete;
    IncomingStatusOnClient& operator=(const IncomingStatusOnClient&) = delete;
    IncomingStatusOnClient(IncomingStatusOnClient&& other) noexcept = default;
    IncomingStatusOnClient& operator=(IncomingStatusOnClient&& other) noexcept =
        default;
    ~IncomingStatusOnClient() {
      if (data_ != nullptr) {
        grpc_metadata_array_destroy(&data_->trailing_metadata);
        gpr_free(const_cast<char*>(data_->error_string));
      }
    }

    grpc_status_code status() const { return data_->status; }
    std::string message() const {
      return std::string(data_->status_details.as_string_view());
    }
    std::string error_string() const {
      return data_->error_string == nullptr ? "" : data_->error_string;
    }
    absl::optional<std::string> GetTrailingMetadata(
        absl::string_view key) const;

    grpc_op MakeOp();

   private:
    struct Data {
      grpc_metadata_array trailing_metadata{0, 0, nullptr};
      grpc_status_code status;
      Slice status_details;
      const char* error_string = nullptr;
    };
    std::unique_ptr<Data> data_ = std::make_unique<Data>();
  };

  class IncomingCloseOnServer {
   public:
    IncomingCloseOnServer() = default;
    IncomingCloseOnServer(const IncomingCloseOnServer&) = delete;
    IncomingCloseOnServer& operator=(const IncomingCloseOnServer&) = delete;

    bool was_cancelled() const { return cancelled_ != 0; }

    grpc_op MakeOp();

   private:
    int cancelled_;
  };

  class BatchBuilder {
   public:
    BatchBuilder(grpc_call* call, int tag) : call_(call), tag_(tag) {}
    ~BatchBuilder();

    BatchBuilder(const BatchBuilder&) = delete;
    BatchBuilder& operator=(const BatchBuilder&) = delete;
    BatchBuilder(BatchBuilder&&) noexcept = default;

    BatchBuilder& SendInitialMetadata(
        std::initializer_list<std::pair<absl::string_view, absl::string_view>>
            md,
        uint32_t flags = 0,
        absl::optional<grpc_compression_level> compression_level =
            absl::nullopt);

    BatchBuilder& SendMessage(Slice payload, uint32_t flags = 0);
    BatchBuilder& SendMessage(absl::string_view payload, uint32_t flags = 0) {
      return SendMessage(Slice::FromCopiedString(payload), flags);
    }

    BatchBuilder& SendCloseFromClient();

    BatchBuilder& SendStatusFromServer(
        grpc_status_code status, absl::string_view message,
        std::initializer_list<std::pair<absl::string_view, absl::string_view>>
            md);

    BatchBuilder& RecvInitialMetadata(IncomingMetadata& md) {
      ops_.emplace_back(md.MakeOp());
      return *this;
    }

    BatchBuilder& RecvMessage(IncomingMessage& msg) {
      ops_.emplace_back(msg.MakeOp());
      return *this;
    }

    BatchBuilder& RecvStatusOnClient(IncomingStatusOnClient& status) {
      ops_.emplace_back(status.MakeOp());
      return *this;
    }

    BatchBuilder& RecvCloseOnServer(IncomingCloseOnServer& close) {
      ops_.emplace_back(close.MakeOp());
      return *this;
    }

   private:
    class Thing {
     public:
      virtual ~Thing() = default;
    };
    template <typename T>
    class SpecificThing final : public Thing {
     public:
      template <typename... Args>
      explicit SpecificThing(Args&&... args)
          : t_(std::forward<Args>(args)...) {}
      SpecificThing() = default;

      T& get() { return t_; }

     private:
      T t_;
    };

    template <typename T, typename... Args>
    T& Make(Args&&... args) {
      things_.emplace_back(new SpecificThing<T>(std::forward<Args>(args)...));
      return static_cast<SpecificThing<T>*>(things_.back().get())->get();
    }

    grpc_call* call_;
    const int tag_;
    std::vector<grpc_op> ops_;
    std::vector<std::unique_ptr<Thing>> things_;
  };

  class Call {
   public:
    explicit Call(grpc_call* call) : call_(call) {}
    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;
    Call(Call&& other) noexcept : call_(std::exchange(other.call_, nullptr)) {}
    ~Call() {
      if (call_ != nullptr) grpc_call_unref(call_);
    }
    BatchBuilder NewBatch(int tag) { return BatchBuilder(call_, tag); }
    void Cancel() { grpc_call_cancel(call_, nullptr); }
    void CancelWithStatus(grpc_status_code status, const char* message) {
      grpc_call_cancel_with_status(call_, status, message, nullptr);
    }
    absl::optional<std::string> GetPeer() {
      char* peer = grpc_call_get_peer(call_);
      if (peer == nullptr) return absl::nullopt;
      std::string result(peer);
      gpr_free(peer);
      return result;
    }

    // Takes ownership of creds
    void SetCredentials(grpc_call_credentials* creds) {
      EXPECT_EQ(grpc_call_set_credentials(call_, creds), GRPC_CALL_OK);
      grpc_call_credentials_release(creds);
    }

    std::unique_ptr<grpc_auth_context, void (*)(grpc_auth_context*)>
    GetAuthContext() {
      return std::unique_ptr<grpc_auth_context, void (*)(grpc_auth_context*)>(
          grpc_call_auth_context(call_), grpc_auth_context_release);
    }

    grpc_call** call_ptr() { return &call_; }
    grpc_call* c_call() const { return call_; }

   private:
    grpc_call* call_ = nullptr;
  };

  class IncomingCall {
   public:
    IncomingCall(CoreEnd2endTest& test, int tag);
    IncomingCall(const IncomingCall&) = delete;
    IncomingCall& operator=(const IncomingCall&) = delete;
    IncomingCall(IncomingCall&&) noexcept = default;

    BatchBuilder NewBatch(int tag) { return impl_->call.NewBatch(tag); }
    void Cancel() { impl_->call.Cancel(); }

    std::string method() const {
      return std::string(StringViewFromSlice(impl_->call_details.method));
    }

    std::string host() const {
      return std::string(StringViewFromSlice(impl_->call_details.host));
    }

    absl::optional<std::string> GetInitialMetadata(absl::string_view key) const;

    absl::optional<std::string> GetPeer() { return impl_->call.GetPeer(); }

    std::unique_ptr<grpc_auth_context, void (*)(grpc_auth_context*)>
    GetAuthContext() {
      return impl_->call.GetAuthContext();
    }

    grpc_call* c_call() { return impl_->call.c_call(); }

    BitSet<GRPC_COMPRESS_ALGORITHMS_COUNT> GetEncodingsAcceptedByPeer() {
      return BitSet<GRPC_COMPRESS_ALGORITHMS_COUNT>::FromInt(
          grpc_call_test_only_get_encodings_accepted_by_peer(c_call()));
    }

   private:
    struct Impl {
      Impl() {
        grpc_call_details_init(&call_details);
        grpc_metadata_array_init(&request_metadata);
      }
      ~Impl() {
        grpc_call_details_destroy(&call_details);
        grpc_metadata_array_destroy(&request_metadata);
      }
      Call call{nullptr};
      grpc_call_details call_details;
      grpc_metadata_array request_metadata;
    };
    std::unique_ptr<Impl> impl_;
  };

  ClientCallBuilder NewClientCall(std::string method) {
    return ClientCallBuilder(*this, std::move(method));
  }
  ClientCallBuilder NewClientCall(void* registered_method) {
    return ClientCallBuilder(*this, registered_method);
  }
  IncomingCall RequestCall(int tag) { return IncomingCall(*this, tag); }

  using ExpectedResult = CqVerifier::ExpectedResult;
  using Maybe = CqVerifier::Maybe;
  using PerformAction = CqVerifier::PerformAction;
  using MaybePerformAction = CqVerifier::MaybePerformAction;
  using AnyStatus = CqVerifier::AnyStatus;
  void Expect(int tag, ExpectedResult result, SourceLocation whence = {}) {
    expectations_++;
    cq_verifier().Expect(CqVerifier::tag(tag), result, whence);
  }
  void Step(absl::optional<Duration> timeout = absl::nullopt,
            SourceLocation whence = {}) {
    if (expectations_ == 0) {
      cq_verifier().VerifyEmpty(timeout.value_or(Duration::Seconds(1)), whence);
      return;
    }
    expectations_ = 0;
    cq_verifier().Verify(timeout.value_or(Duration::Seconds(10)), whence);
  }

  void InitClient(const ChannelArgs& args) {
    initialized_ = true;
    fixture().InitClient(args);
  }
  void InitServer(const ChannelArgs& args) {
    initialized_ = true;
    fixture().InitServer(args);
  }
  void ShutdownAndDestroyClient() { fixture().ShutdownClient(); }
  void ShutdownServerAndNotify(int tag) {
    grpc_server_shutdown_and_notify(fixture().server(), fixture().cq(),
                                    CqVerifier::tag(tag));
  }
  void DestroyServer() { fixture().DestroyServer(); }
  void ShutdownAndDestroyServer() { fixture().ShutdownServer(); }
  void CancelAllCallsOnServer() {
    grpc_server_cancel_all_calls(fixture().server());
  }
  void PingServerFromClient(int tag) {
    grpc_channel_ping(fixture().client(), fixture().cq(), CqVerifier::tag(tag),
                      nullptr);
  }
  void* RegisterCallOnClient(const char* method, const char* host) {
    ForceInitialized();
    return grpc_channel_register_call(fixture().client(), method, host,
                                      nullptr);
  }

  grpc_connectivity_state CheckConnectivityState(bool try_to_connect) {
    return grpc_channel_check_connectivity_state(fixture().client(),
                                                 try_to_connect);
  }

  void WatchConnectivityState(grpc_connectivity_state last_observed_state,
                              Duration deadline, int tag) {
    grpc_channel_watch_connectivity_state(
        fixture().client(), last_observed_state,
        grpc_timeout_milliseconds_to_deadline(deadline.millis()),
        fixture().cq(), CqVerifier::tag(tag));
  }

  grpc_channel* client() {
    ForceInitialized();
    return fixture().client();
  }

  grpc_server* server() {
    ForceInitialized();
    return fixture().server();
  }

  Timestamp TimestampAfterDuration(Duration duration) {
    return Timestamp::FromTimespecRoundUp(
        grpc_timeout_milliseconds_to_deadline(duration.millis()));
  }

 private:
  void ForceInitialized();

  CoreTestFixture& fixture() {
    if (fixture_ == nullptr) {
      grpc_init();
      fixture_ = GetParam()->create_fixture(ChannelArgs(), ChannelArgs());
    }
    return *fixture_;
  }

  CqVerifier& cq_verifier() {
    if (cq_verifier_ == nullptr) {
      cq_verifier_ = absl::make_unique<CqVerifier>(fixture().cq());
    }
    return *cq_verifier_;
  }

  std::unique_ptr<CoreTestFixture> fixture_;
  std::unique_ptr<CqVerifier> cq_verifier_;
  int expectations_ = 0;
  bool initialized_ = false;
};

class SecureEnd2endTest : public CoreEnd2endTest {};
class CoreLargeSendTest : public CoreEnd2endTest {};
class CoreClientChannelTest : public CoreEnd2endTest {};
class CoreDeadlineTest : public CoreEnd2endTest {};
class Http2SingleHopTest : public CoreEnd2endTest {};
class RetryTest : public CoreEnd2endTest {};
class WriteBufferingTest : public CoreEnd2endTest {};
class Http2Test : public CoreEnd2endTest {};
class RetryHttp2Test : public CoreEnd2endTest {};
class ResourceQuotaTest : public CoreEnd2endTest {};
class PerCallCredsTest : public CoreEnd2endTest {};
class PerCallCredsOnInsecureTest : public CoreEnd2endTest {};
class NoLoggingTest : public CoreEnd2endTest {};
class ProxyAuthTest : public CoreEnd2endTest {};

}  // namespace grpc_core

#define SKIP_IF_MINSTACK()                                 \
  if (GetParam()->feature_mask & FEATURE_MASK_IS_MINSTACK) \
  GTEST_SKIP() << "Skipping test for minstack"

void grpc_end2end_tests_pre_init(void);
void grpc_end2end_tests(int argc, char** argv,
                        const grpc_core::CoreTestConfiguration& config);

const char* get_host_override_string(
    const char* str, const grpc_core::CoreTestConfiguration& config);
// Returns a pointer to a statically allocated slice: future invocations
// overwrite past invocations, not threadsafe, etc...
const grpc_slice* get_host_override_slice(
    const char* str, const grpc_core::CoreTestConfiguration& config);

void validate_host_override_string(
    const char* pattern, grpc_slice str,
    const grpc_core::CoreTestConfiguration& config);

#endif  // GRPC_TEST_CORE_END2END_END2END_TESTS_H
