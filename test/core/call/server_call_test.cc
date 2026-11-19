// Copyright 2024 gRPC authors.
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

#include "src/core/call/server_call.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/status.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/core/call/message.h"
#include "src/core/channelz/channelz.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/time.h"
#include "test/core/call/batch_builder.h"
#include "test/core/call/yodel/yodel_test.h"
#include "test/core/end2end/cq_verifier.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {

namespace {
const absl::string_view kDefaultPath = "/foo/bar";
}

class ServerCallTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  ~ServerCallTest() override {
    grpc_metadata_array_destroy(&publish_initial_metadata_);
  }

  grpc_call* InitCall(ClientMetadataHandle client_initial_metadata) {
    CHECK_EQ(call_, nullptr);
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine().get());
    auto call =
        MakeCallPair(std::move(client_initial_metadata), std::move(arena));
    call_initiator_ = call.initiator;
    call.initiator.SpawnGuarded(
        "initial_metadata",
        [this, handler = call.handler.StartCall()]() mutable {
          return TrySeq(
              handler.PullClientInitialMetadata(),
              [this,
               handler](ClientMetadataHandle client_initial_metadata) mutable {
                call_.store(MakeServerCall(std::move(handler),
                                           std::move(client_initial_metadata),
                                           &test_server_, cq_,
                                           &publish_initial_metadata_, nullptr),
                            std::memory_order_release);
                return absl::OkStatus();
              });
        });
    while (true) {
      auto* result = call_.load(std::memory_order_acquire);
      if (result != nullptr) return result;
    }
  }

  ClientMetadataHandle MakeClientInitialMetadata(
      std::initializer_list<std::pair<absl::string_view, absl::string_view>>
          md) {
    auto client_initial_metadata =
        Arena::MakePooledForOverwrite<ClientMetadata>();
    client_initial_metadata->Set(HttpPathMetadata(),
                                 Slice::FromCopiedString(kDefaultPath));
    for (const auto& pair : md) {
      client_initial_metadata->Append(
          pair.first, Slice::FromCopiedBuffer(pair.second),
          [](absl::string_view error, const Slice&) { Crash(error); });
    }
    return client_initial_metadata;
  }

  std::optional<std::string> GetClientInitialMetadata(absl::string_view key) {
    CHECK_NE(call_.load(std::memory_order_acquire), nullptr);
    return FindInMetadataArray(publish_initial_metadata_, key);
  }

  BatchBuilder NewBatch(int tag) {
    return BatchBuilder(call_.load(std::memory_order_acquire),
                        cq_verifier_.get(), tag);
  }

  // Pushes a message from the client side of the call.
  void ClientSendMessage(absl::string_view payload) {
    SliceBuffer slice_buffer;
    slice_buffer.Append(Slice::FromCopiedString(payload));
    call_initiator_.SpawnPushMessage(
        Arena::MakePooled<Message>(std::move(slice_buffer), 0u));
  }

  // Half-closes the client side of the call (no more client messages).
  void ClientHalfClose() { call_initiator_.SpawnFinishSends(); }

  using ExpectedResult = CqVerifier::ExpectedResult;
  void Expect(int tag, ExpectedResult result, SourceLocation whence = {}) {
    expectations_++;
    cq_verifier_->Expect(CqVerifier::tag(tag), std::move(result), whence);
  }

  void TickThroughCqExpectations(std::optional<Duration> timeout = std::nullopt,
                                 SourceLocation whence = {}) {
    if (expectations_ == 0) {
      cq_verifier_->VerifyEmpty(timeout.value_or(Duration::Seconds(1)), whence);
      return;
    }
    expectations_ = 0;
    cq_verifier_->Verify(timeout.value_or(Duration::Minutes(5)), whence);
  }

 private:
  class TestServer final : public ServerInterface {
   public:
    const ChannelArgs& channel_args() const override { return channel_args_; }
    channelz::ServerNode* channelz_node() const override { return nullptr; }
    ServerCallTracerFactory* server_call_tracer_factory() const override {
      return nullptr;
    }
    grpc_compression_options compression_options() const override {
      return {
          1,
          {0, GRPC_COMPRESS_LEVEL_NONE},
          {0, GRPC_COMPRESS_NONE},
      };
    }

   private:
    ChannelArgs channel_args_;
  };

  void InitTest() override {
    cq_ = grpc_completion_queue_create_for_next(nullptr);
    cq_verifier_ = std::make_unique<CqVerifier>(
        cq_, CqVerifier::FailUsingGprCrash,
        [this](
            grpc_event_engine::experimental::EventEngine::Duration max_step) {
          event_engine()->Tick(max_step);
        });
  }

  void Shutdown() override {
    auto* call = call_.load(std::memory_order_acquire);
    if (call != nullptr) {
      grpc_call_unref(call);
    }
    // Drop the client side of the call so the call spine can be destroyed.
    call_initiator_ = CallInitiator();
    grpc_completion_queue_shutdown(cq_);
    auto ev = grpc_completion_queue_next(
        cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    CHECK_EQ(ev.type, GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cq_);
  }

  grpc_completion_queue* cq_{nullptr};
  std::atomic<grpc_call*> call_{nullptr};
  CallInitiator call_initiator_;
  TestServer test_server_;
  grpc_metadata_array publish_initial_metadata_{0, 0, nullptr};
  std::unique_ptr<CqVerifier> cq_verifier_;
  int expectations_ = 0;
};

#define SERVER_CALL_TEST(name) YODEL_TEST(ServerCallTest, name)

SERVER_CALL_TEST(NoOp) { InitCall(MakeClientInitialMetadata({})); }

SERVER_CALL_TEST(InitialMetadataPassedThrough) {
  InitCall(MakeClientInitialMetadata({{"foo", "bar"}}));
  EXPECT_EQ(GetClientInitialMetadata("foo"), "bar");
}

// Asserts that sending status on an active call succeeds.
SERVER_CALL_TEST(SendStatusFromServerSucceeds) {
  InitCall(MakeClientInitialMetadata({}));
  NewBatch(1).SendStatusFromServer(GRPC_STATUS_OK, "ok", {});
  Expect(1, true);
  TickThroughCqExpectations();
  WaitForAllPendingWork();
}

// Asserts that sending status after the call was cancelled fails because
// cancellation already pushed the server trailing metadata, and that
// RECV_CLOSE_ON_SERVER reports the call as cancelled.
SERVER_CALL_TEST(SendStatusFromServerAfterCancelFails) {
  grpc_call* const call = InitCall(MakeClientInitialMetadata({}));
  EXPECT_EQ(grpc_call_cancel(call, nullptr), GRPC_CALL_OK);
  WaitForAllPendingWork();
  IncomingCloseOnServer close;
  NewBatch(1).SendStatusFromServer(GRPC_STATUS_OK, "ok", {});
  NewBatch(2).RecvCloseOnServer(close);
  Expect(1, false);
  Expect(2, true);
  TickThroughCqExpectations();
  EXPECT_TRUE(close.was_cancelled());
  WaitForAllPendingWork();
}

// Asserts that grpc_call_failed_before_recv_message() stays false when the
// server receives a message and then observes a clean client half-close:
// a RECV_MESSAGE that completes with no message because the client is done
// sending is a graceful end-of-stream, not a failure.
SERVER_CALL_TEST(FailedBeforeRecvMessageIsFalseOnCleanHalfClose) {
  grpc_call* const call = InitCall(MakeClientInitialMetadata({}));
  // Nothing has been received yet: the flag starts out false.
  EXPECT_FALSE(grpc_call_failed_before_recv_message(call));
  // Step 1: the client sends one message and half-closes.
  ClientSendMessage("hello");
  ClientHalfClose();
  // Step 2: the server reads the message successfully.
  IncomingMessage first_message;
  NewBatch(1).RecvMessage(first_message);
  Expect(1, true);
  TickThroughCqExpectations();
  EXPECT_FALSE(first_message.is_end_of_stream());
  EXPECT_EQ(first_message.payload(), "hello");
  EXPECT_FALSE(grpc_call_failed_before_recv_message(call));
  // Step 3: the next read sees end-of-stream (no message) with an OK batch,
  // and the call must NOT be reported as failed.
  IncomingMessage end_of_stream;
  NewBatch(2).RecvMessage(end_of_stream);
  Expect(2, true);
  TickThroughCqExpectations();
  EXPECT_TRUE(end_of_stream.is_end_of_stream());
  EXPECT_FALSE(grpc_call_failed_before_recv_message(call));
  WaitForAllPendingWork();
}

// Asserts that grpc_call_failed_before_recv_message() becomes true when a
// pending RECV_MESSAGE completes without a message because the call was
// cancelled. This is what ServerContext::IsCancelled() relies on (via
// ServerContextBase::MaybeMarkCancelledOnRead()) to tell a cancellation apart
// from a clean half-close, and it is read from the application thread.
SERVER_CALL_TEST(FailedBeforeRecvMessageIsTrueAfterCancel) {
  grpc_call* const call = InitCall(MakeClientInitialMetadata({}));
  EXPECT_FALSE(grpc_call_failed_before_recv_message(call));
  // Step 1: start a read that cannot complete yet (no client message).
  IncomingMessage message;
  NewBatch(1).RecvMessage(message);
  // Step 2: cancel the call while the read is outstanding.
  EXPECT_EQ(grpc_call_cancel(call, nullptr), GRPC_CALL_OK);
  // Step 3: the read fails with no message, and the call is reported as
  // having failed before a message was received.
  Expect(1, false);
  TickThroughCqExpectations();
  EXPECT_TRUE(message.is_end_of_stream());
  EXPECT_TRUE(grpc_call_failed_before_recv_message(call));
  WaitForAllPendingWork();
}

}  // namespace grpc_core
