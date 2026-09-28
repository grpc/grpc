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

// Tests the correct processing of inbound (server-to-client) messages in the
// client-side promise-based filter stack when server initial metadata is
// stalled.
//
// Scenario:
//   1. A promise-based filter stalls server initial metadata and mutates every
//      server->client message.
//   2. While initial metadata is stalled, the transport delivers a message.
//      The message is parked because initial metadata has not yet been
//      delivered to the application.
//   3. The transport then delivers trailing metadata (or the call is
//   cancelled).
//
// Asserts that the parked message is correctly processed and mutated by all
// filters in the stack before delivery to the application.

#include <grpc/grpc.h>
#include <grpc/status.h>

#include <memory>
#include <utility>

#include "src/core/call/message.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/slice/slice.h"
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

constexpr char kMutationSuffix[] = "_INTERCEPTED";

// Coordinates initial metadata stalling and resumption between the test and the
// mock filter.
struct MetadataStallController {
  struct RawPointerChannelArgTag {};
  static absl::string_view ChannelArgName() {
    return "grpc.test.metadata_stall_controller";
  }
  // Set to true to release the stalled metadata batch.
  bool release_initial_md = false;
  // Captures the call's promise waker when it stalls, allowing the test to
  // wake it up.
  Waker init_md_waker;
  // Set to true to make filter's main promise resolve with and out-of-band
  // immediate response (a non-OK ServerMetadata), replacing the downstream
  // response.
  bool trigger_immediate_response = false;
  // Capture waker
  Waker immediate_response_waker;
};

// A client filter that stalls server initial metadata until released and
// appends kMutationSuffix to every inbound message.
// If test set control->trigger_immediate_response, the filter main promise
// also resolves with and out-of-band immediate response, (example ext_proc
// filter that replaces server response while a message is still parked behind
// stalled initial metadata. )
class StallInitialMetadataMutateMessageFilter final : public ChannelFilter {
 public:
  explicit StallInitialMetadataMutateMessageFilter(
      MetadataStallController* control)
      : control_(control) {}

  static absl::string_view TypeName() { return "stall_mutate_test_filter"; }

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    // Stall server initial metadata until released by the test.
    args.server_initial_metadata->InterceptAndMap(
        [this](ServerMetadataHandle md) {
          return [md = std::move(md),
                  this]() mutable -> Poll<ServerMetadataHandle> {
            if (control_ != nullptr && control_->release_initial_md) {
              return std::move(md);
            }
            if (control_ != nullptr) {
              control_->init_md_waker =
                  GetContext<Activity>()->MakeNonOwningWaker();
            }
            return Pending{};
          };
        });
    // Mutate every inbound (server->client) message. This is the hook that the
    // buggy bypass path skips.
    args.server_to_client_messages->InterceptAndMap([](MessageHandle msg) {
      msg->payload()->Append(Slice::FromCopiedString(kMutationSuffix));
      return msg;
    });
    auto inner = next(std::move(args));
    return [inner = std::move(inner),
            this]() mutable -> Poll<ServerMetadataHandle> {
      if (control_ != nullptr && control_->trigger_immediate_response) {
        // short circuit call with immediate response
        return ServerMetadataFromStatus(GRPC_STATUS_PERMISSION_DENIED,
                                        "Access denied by filter");
      }
      if (control_ != nullptr) {
        control_->immediate_response_waker =
            GetContext<Activity>()->MakeNonOwningWaker();
      }
      return inner();
    };
  }

  static absl::StatusOr<
      std::unique_ptr<StallInitialMetadataMutateMessageFilter>>
  Create(const ChannelArgs& args, ChannelFilter::Args) {
    return std::make_unique<StallInitialMetadataMutateMessageFilter>(
        args.GetObject<MetadataStallController>());
  }

 private:
  MetadataStallController* control_;
};

const grpc_channel_filter kStallFilter = MakePromiseBasedFilter<
    StallInitialMetadataMutateMessageFilter, FilterEndpoint::kClient,
    kFilterExaminesServerInitialMetadata | kFilterExaminesInboundMessages>();

class AppendSuffixAFilter final : public ChannelFilter {
 public:
  static absl::string_view TypeName() { return "append_suffix_a_filter"; }

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    args.server_to_client_messages->InterceptAndMap([](MessageHandle msg) {
      msg->payload()->Append(Slice::FromCopiedString("_A"));
      return msg;
    });
    return next(std::move(args));
  }

  static absl::StatusOr<std::unique_ptr<AppendSuffixAFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<AppendSuffixAFilter>();
  }
};

const grpc_channel_filter kFilterA =
    MakePromiseBasedFilter<AppendSuffixAFilter, FilterEndpoint::kClient,
                           kFilterExaminesInboundMessages>();

// The shared harness plus the stall controller wiring: the controller is
// published to the filter via a channel arg, and its wakers must be dropped
// before the call stack (and hence the arena they point into) goes away, which
// the derived destructor guarantees.
class StallFakeCallStack : public FakeCallStack {
 public:
  StallFakeCallStack(std::vector<FilterAndConfig> filters,
                     MetadataStallController* control)
      : FakeCallStack(std::move(filters),
                      control == nullptr ? ChannelArgs()
                                         : ChannelArgs().SetObject(control)),
        control_(control) {
    StartStandardBatches();
  }

  ~StallFakeCallStack() override {
    // Reset wakers to prevent call stack from outliving the arena.
    if (control_ != nullptr) {
      control_->init_md_waker = Waker();
      control_->immediate_response_waker = Waker();
    }
  }

 private:
  MetadataStallController* control_;
};

// Clean completion: server sends a message then OK trailing metadata while the
// filter stalls server initial metadata. The message must be delivered through
// the filter (mutated), not bypassed.
TEST(RecvMessageFilterBypassTest,
     InboundMessageFilteredWhenInitialMetadataStalledOkTrailing) {
  if (!IsRecvMessageFilterBypassFixEnabled()) {
    GTEST_SKIP() << "Test fail without experiment";
  }
  ExecCtx exec_ctx;
  // Create Call stack StallFilter - Transport
  MetadataStallController control;
  StallFakeCallStack env(
      {{&kStallFilter, nullptr}, {&MockTransportFilter::kFilter, nullptr}},
      &control);
  // 1) Transport delivers server initial metadata; filter stalls it.
  env.mock.recv_initial_metadata->Set(HttpStatusMetadata(), 200);
  env.RunOnCombiner(env.mock.recv_initial_metadata_ready);
  EXPECT_FALSE(env.app.init_md_ready_called);
  // 2) Transport delivers a message; it must be parked.
  {
    SliceBuffer sb;
    sb.Append(Slice::FromCopiedString("hello"));
    *env.mock.recv_message = std::move(sb);
  }
  env.RunOnCombiner(env.mock.recv_message_ready);
  EXPECT_FALSE(env.app.msg_ready_called);
  // 3) Transport delivers trailing metadata (OK).
  env.recv_trail_md.Set(GrpcStatusMetadata(), GRPC_STATUS_OK);
  env.RunOnCombiner(env.mock.recv_trailing_metadata_ready);
  EXPECT_FALSE(env.app.msg_ready_called);
  // 4) Release server initial metadata.
  control.release_initial_md = true;
  control.init_md_waker.Wakeup();
  ExecCtx::Get()->Flush();
  // Verify parked message is flushed and mutated by filter.
  EXPECT_TRUE(env.app.init_md_ready_called);
  EXPECT_TRUE(env.app.msg_ready_called);
  EXPECT_EQ(env.app.captured_payload, std::string("hello") + kMutationSuffix);
}

// Same, but the server completes with a non-OK status. The received
// message must still be delivered through the filter (mutated), not dropped or
// bypassed.
TEST(RecvMessageFilterBypassTest,
     InboundMessageFilteredWhenInitialMetadataStalledNonOkTrailing) {
  if (!IsRecvMessageFilterBypassFixEnabled()) {
    GTEST_SKIP() << "Test fail without experiment";
  }
  ExecCtx exec_ctx;
  // Create call stack
  MetadataStallController control;
  StallFakeCallStack env(
      {{&kStallFilter, nullptr}, {&MockTransportFilter::kFilter, nullptr}},
      &control);
  // 1) Transport delivers server initial metadata; filter stalls it.
  env.mock.recv_initial_metadata->Set(HttpStatusMetadata(), 200);
  env.RunOnCombiner(env.mock.recv_initial_metadata_ready);
  EXPECT_FALSE(env.app.init_md_ready_called);
  // 2) Transport delivers a message; it must be parked.
  {
    SliceBuffer sb;
    sb.Append(Slice::FromCopiedString("hello"));
    *env.mock.recv_message = std::move(sb);
  }
  env.RunOnCombiner(env.mock.recv_message_ready);
  EXPECT_FALSE(env.app.msg_ready_called);
  // 3) Transport delivers trailing metadata (non-OK status).
  env.recv_trail_md.Set(GrpcStatusMetadata(), GRPC_STATUS_UNAVAILABLE);
  env.RunOnCombiner(env.mock.recv_trailing_metadata_ready);
  EXPECT_FALSE(env.app.msg_ready_called);
  // 4) Release server initial metadata. Parked message is flushed and mutated.
  control.release_initial_md = true;
  control.init_md_waker.Wakeup();
  ExecCtx::Get()->Flush();
  // Verify parked message is flushed and mutated by filter.
  EXPECT_TRUE(env.app.init_md_ready_called);
  EXPECT_TRUE(env.app.msg_ready_called);
  EXPECT_EQ(env.app.captured_payload, std::string("hello") + kMutationSuffix);
}

// Two promise-based filters in the stack:
//   Filter A (higher) - mutates message with suffix "_A"
//   Filter B (lower)  - stalls server initial metadata, mutates message with
//                       kMutationSuffix ("_INTERCEPTED")
// Trailing metadata is received while B is stalling initial metadata.
TEST(RecvMessageFilterBypassTest,
     InboundMessageFilteredWhenInitialMetadataStalledOkTrailingTwoFilters) {
  if (!IsRecvMessageFilterBypassFixEnabled()) {
    GTEST_SKIP() << "Test fail without experiment";
  }
  ExecCtx exec_ctx;
  // Create call stack
  MetadataStallController control;
  StallFakeCallStack env({{&kFilterA, nullptr},
                          {&kStallFilter, nullptr},
                          {&MockTransportFilter::kFilter, nullptr}},
                         &control);
  // 1) Transport delivers server initial metadata; filter B stalls it.
  env.mock.recv_initial_metadata->Set(HttpStatusMetadata(), 200);
  env.RunOnCombiner(env.mock.recv_initial_metadata_ready);
  EXPECT_FALSE(env.app.init_md_ready_called);
  // 2) Transport delivers a message; it must be parked.
  {
    SliceBuffer sb;
    sb.Append(Slice::FromCopiedString("hello"));
    *env.mock.recv_message = std::move(sb);
  }
  env.RunOnCombiner(env.mock.recv_message_ready);
  EXPECT_FALSE(env.app.msg_ready_called);
  // 3) Transport delivers trailing metadata (OK status).
  env.recv_trail_md.Set(GrpcStatusMetadata(), GRPC_STATUS_OK);
  env.RunOnCombiner(env.mock.recv_trailing_metadata_ready);
  EXPECT_FALSE(env.app.msg_ready_called);
  // 4) Release server initial metadata.
  control.release_initial_md = true;
  control.init_md_waker.Wakeup();
  ExecCtx::Get()->Flush();
  // Verify parked message is flushed and mutated by both filters.
  EXPECT_TRUE(env.app.init_md_ready_called);
  EXPECT_TRUE(env.app.msg_ready_called);
  EXPECT_EQ(env.app.captured_payload,
            std::string("hello") + kMutationSuffix + "_A");
}

// Out-of-band cancellation while a message is parked. The parked message
// must be discarded, and the message read callback must return failed status.
TEST(RecvMessageFilterBypassTest, StalledMessageCancelled) {
  if (!IsRecvMessageFilterBypassFixEnabled()) {
    GTEST_SKIP() << "Test fail without experiment";
  }
  ExecCtx exec_ctx;
  // Create CallStack
  MetadataStallController control;
  StallFakeCallStack env(
      {{&kStallFilter, nullptr}, {&MockTransportFilter::kFilter, nullptr}},
      &control);
  // 1) Stalled initial metadata.
  env.mock.recv_initial_metadata->Set(HttpStatusMetadata(), 200);
  env.RunOnCombiner(env.mock.recv_initial_metadata_ready);
  // 2) Parked message.
  {
    SliceBuffer sb;
    sb.Append(Slice::FromCopiedString("hello"));
    *env.mock.recv_message = std::move(sb);
  }
  env.RunOnCombiner(env.mock.recv_message_ready);
  // 3) Out-of-band cancellation.
  env.CancelStream(absl::CancelledError());
  // Verify message is cancelled.
  EXPECT_TRUE(env.app.msg_ready_called);
  EXPECT_FALSE(env.app.msg_status.ok());
  EXPECT_EQ(env.app.captured_payload, "");
  // Now transport returns cancelled trailing metadata.
  env.recv_trail_md.Set(GrpcStatusMetadata(), GRPC_STATUS_CANCELLED);
  env.RunOnCombiner(env.mock.recv_trailing_metadata_ready);
  EXPECT_TRUE(env.app.trailing_ready_called);
}

// Test Stalled message is discarded for immediate non-ok response
TEST(RecvMessageFilterBypassTest,
     StalledMessageImmediateResponseDiscardsParkedMessage) {
  if (!IsRecvMessageFilterBypassFixEnabled()) {
    GTEST_SKIP() << "Test fail without experiment";
  }
  ExecCtx exec_ctx;
  // Create CallStack
  MetadataStallController control;
  StallFakeCallStack env(
      {{&kStallFilter, nullptr}, {&MockTransportFilter::kFilter, nullptr}},
      &control);
  // 1) Stalled initial metadata.
  env.mock.recv_initial_metadata->Set(HttpStatusMetadata(), 200);
  env.RunOnCombiner(env.mock.recv_initial_metadata_ready);
  // 2) Parked message.
  {
    SliceBuffer sb;
    sb.Append(Slice::FromCopiedString("hello"));
    *env.mock.recv_message = std::move(sb);
  }
  env.RunOnCombiner(env.mock.recv_message_ready);
  // 3) Transport deliver OK trailing metadata parked
  env.recv_trail_md.Set(GrpcStatusMetadata(), GRPC_STATUS_OK);
  env.RunOnCombiner(env.mock.recv_trailing_metadata_ready);
  EXPECT_FALSE(env.app.msg_ready_called);
  // 4) The filter's promise now produces an out-of-band immediate response
  // (non-ok) while the message is still parked.
  control.trigger_immediate_response = true;
  control.immediate_response_waker.Wakeup();
  ExecCtx::Get()->Flush();
  // The parked message must be discarded (its read callback completed with a
  // failed status and no payload) and the
  //  call must finish.
  EXPECT_TRUE(env.app.msg_ready_called);
  EXPECT_FALSE(env.app.msg_status.ok());
  EXPECT_EQ(env.app.captured_payload, "");
  EXPECT_TRUE(env.app.trailing_ready_called);
}
}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_core::ForceEnableExperiment("recv_message_filter_bypass_fix", true);
  grpc_core::ForceEnableExperiment("recv_message_cancelled_status_fix", true);
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
