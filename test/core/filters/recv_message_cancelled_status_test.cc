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

// Tests the status reported to the application when a recv_message batch
// completes *after* the call has been cancelled.
//
// Scenario:
//   1. A recv_message batch is forwarded down to the transport, so
//      BaseCallData::ReceiveMessage sits in kForwardedBatch(NoPipe).
//   2. The call is cancelled out of band.  ReceiveMessage::Done() moves to
//      kCancelledWhilstForwarding(NoPipe) and records the cancellation status.
//   3. The transport only then completes the recv_message batch, with an OK
//      batch status and no message.  ReceiveMessage::OnComplete() moves to
//      kBatchCompletedButCancelled(NoPipe) and overwrites completed_status_
//      with the (OK) batch status.
//
// Asserts that the application's recv_message_ready callback observes the
// cancellation status rather than the OK status of the batch itself.

#include <grpc/grpc.h>

#include <memory>
#include <utility>

#include "src/core/call/message.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/arena_promise.h"
#include "test/core/filters/fake_call_stack.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

using ::grpc_core::testing::FakeCallStack;
using ::grpc_core::testing::MockTransportFilter;

// A client filter that declares an interest in inbound messages.  It is only
// here to put a promise based filter (and hence a BaseCallData::ReceiveMessage)
// in the stack; it does not modify anything.
class InboundMessageFilter final : public ChannelFilter {
 public:
  static absl::string_view TypeName() { return "inbound_message_test_filter"; }

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    args.server_to_client_messages->InterceptAndMap(
        [](MessageHandle msg) { return msg; });
    return next(std::move(args));
  }

  static absl::StatusOr<std::unique_ptr<InboundMessageFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<InboundMessageFilter>();
  }
};

const grpc_channel_filter kInboundMessageFilter =
    MakePromiseBasedFilter<InboundMessageFilter, FilterEndpoint::kClient,
                           kFilterExaminesInboundMessages>();

// The recv_message batch is in flight at the transport when the call is
// cancelled, and the transport completes it (with an OK batch status)
// afterwards: ReceiveMessage goes kForwardedBatch -> kCancelledWhilstForwarding
// -> kBatchCompletedButCancelled.  The application must see the cancellation
// status, not the OK status of the batch.
TEST(RecvMessageCancelledStatusTest, CancelBeforeBatchCompletion) {
  if (!IsRecvMessageCancelledStatusFixEnabled()) {
    GTEST_SKIP() << "Test requires recv_message_cancelled_status_fix";
  }
  ExecCtx exec_ctx;
  FakeCallStack env({{&kInboundMessageFilter, nullptr},
                     {&MockTransportFilter::kFilter, nullptr}});
  // 1) Start the call; the filter's promise (and hence the message pipe) now
  // exists.
  env.StartStandardBatches();
  // 2) Server initial metadata arrives, so the call is streaming normally.
  env.mock.recv_initial_metadata->Set(HttpStatusMetadata(), 200);
  env.RunOnCombiner(env.mock.recv_initial_metadata_ready);
  EXPECT_TRUE(env.app.init_md_ready_called);
  // 3) Out of band cancellation while the recv_message batch is still with the
  // transport.
  ASSERT_NE(env.mock.recv_message_ready, nullptr);
  env.CancelStream(
      absl::Status(absl::StatusCode::kDeadlineExceeded, "test cancel"));
  EXPECT_FALSE(env.app.msg_ready_called);
  // 4) Only now does the transport complete the recv_message batch, with an OK
  // batch status and no message.
  env.RunOnCombiner(env.mock.recv_message_ready);
  // The application must observe the cancellation status.
  EXPECT_TRUE(env.app.msg_ready_called);
  EXPECT_EQ(env.app.msg_status.code(), absl::StatusCode::kDeadlineExceeded)
      << env.app.msg_status;
}

// Same ordering, but the recv_message batch is issued before the promise
// exists, so ReceiveMessage takes the "no pipe" arm of the state machine:
// kForwardedBatchNoPipe -> kCancelledWhilstForwardingNoPipe ->
// kBatchCompletedButCancelledNoPipe.
TEST(RecvMessageCancelledStatusTest, CancelBeforeBatchCompletionNoPipe) {
  if (!IsRecvMessageCancelledStatusFixEnabled()) {
    GTEST_SKIP() << "Test requires recv_message_cancelled_status_fix";
  }
  ExecCtx exec_ctx;
  FakeCallStack env({{&kInboundMessageFilter, nullptr},
                     {&MockTransportFilter::kFilter, nullptr}});
  // 1) recv_message is issued before send_initial_metadata, so the filter has
  // not been given a message pipe yet.
  env.StartRecvMessageBatch();
  env.StartInitialMetadataBatch();
  env.StartRecvTrailingMetadataBatch();
  env.mock.recv_initial_metadata->Set(HttpStatusMetadata(), 200);
  env.RunOnCombiner(env.mock.recv_initial_metadata_ready);
  // 2) Out of band cancellation while the recv_message batch is still with the
  // transport.
  ASSERT_NE(env.mock.recv_message_ready, nullptr);
  env.CancelStream(
      absl::Status(absl::StatusCode::kDeadlineExceeded, "test cancel"));
  EXPECT_FALSE(env.app.msg_ready_called);
  // 3) The transport completes the recv_message batch with an OK batch status.
  env.RunOnCombiner(env.mock.recv_message_ready);
  EXPECT_TRUE(env.app.msg_ready_called);
  EXPECT_EQ(env.app.msg_status.code(), absl::StatusCode::kDeadlineExceeded)
      << env.app.msg_status;
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_core::ForceEnableExperiment("recv_message_cancelled_status_fix", true);
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
