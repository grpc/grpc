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

// Encapsulates the mock server call stack, a server-side filter that observes
// client-to-server half-close, and helper methods to drive server call batches.
class FakeServerCallStack {
 public:
  struct Controller {
    struct RawPointerChannelArgTag {};
    static absl::string_view ChannelArgName() {
      return "grpc.test.server_filter_controller";
    }
    bool half_close_observed = false;
  };

  class Filter final : public ChannelFilter {
   public:
    explicit Filter(Controller* controller) : controller_(controller) {}

    static absl::string_view TypeName() {
      return "server_observe_half_close_filter";
    }

    ArenaPromise<ServerMetadataHandle> MakeCallPromise(
        CallArgs args, NextPromiseFactory next) override {
      auto next_message = args.client_to_server_messages->Next();
      auto inner = next(std::move(args));
      return
          [next_message = std::move(next_message), inner = std::move(inner),
           controller = controller_]() mutable -> Poll<ServerMetadataHandle> {
            auto p = next_message();
            if (auto* r = p.value_if_ready()) {
              if (!r->has_value()) {
                if (controller != nullptr) {
                  controller->half_close_observed = true;
                }
              }
            }
            if (controller != nullptr && !controller->half_close_observed) {
              return Pending{};
            }
            return inner();
          };
    }

    static absl::StatusOr<std::unique_ptr<Filter>> Create(
        const ChannelArgs& args, ChannelFilter::Args) {
      return std::make_unique<Filter>(args.GetObject<Controller>());
    }

   private:
    Controller* controller_;
  };

  static inline const grpc_channel_filter kFilter =
      MakePromiseBasedFilter<Filter, FilterEndpoint::kServer,
                             kFilterExaminesInboundMessages>();

  FakeServerCallStack() {
    mock.call_combiner = &call_combiner;
    auto channel_args = CoreConfiguration::Get()
                            .channel_args_preconditioning()
                            .PreconditionChannelArgs(nullptr)
                            .SetObject(&mock)
                            .SetObject(&controller);
    std::vector<FilterAndConfig> filters = {
        {&kFilter, nullptr},
        {&MockTransportFilter::kFilter, nullptr},
    };
    channel_stack = static_cast<grpc_channel_stack*>(
        gpr_malloc(grpc_channel_stack_size(filters)));
    GRPC_CHECK_OK(grpc_channel_stack_init(
        1,
        [](void* p, grpc_error_handle) {
          grpc_channel_stack_destroy(static_cast<grpc_channel_stack*>(p));
          gpr_free(p);
        },
        channel_stack, filters, channel_args, "test", channel_stack));
    arena = SimpleArenaAllocator()->MakeArena();
    call_stack = static_cast<grpc_call_stack*>(
        gpr_malloc(channel_stack->call_stack_size));
    const grpc_call_element_args call_args = {
        call_stack,
        nullptr,
        gpr_get_cycle_counter(),
        Timestamp::InfFuture(),
        arena.get(),
        &call_combiner,
    };
    GRPC_CHECK_OK(grpc_call_stack_init(
        channel_stack, 1,
        [](void* p, grpc_error_handle) {
          grpc_call_stack_destroy(static_cast<grpc_call_stack*>(p), nullptr,
                                  nullptr);
          gpr_free(p);
        },
        call_stack, &call_args));

    top = grpc_call_stack_element(call_stack, 0);
  }

  ~FakeServerCallStack() {
    GRPC_CALL_STACK_UNREF(call_stack, "done");
    ExecCtx::Get()->Flush();
    GRPC_CHANNEL_STACK_UNREF(channel_stack, "done");
  }

  void RunOnCombiner(grpc_closure* c) {
    GRPC_CALL_COMBINER_START(&call_combiner, c, absl::OkStatus(), "test");
    ExecCtx::Get()->Flush();
  }

  void StartRecvInitialMetadata() {
    batch_recv_init.payload = &payload_recv_init;
    batch_recv_init.recv_initial_metadata = true;
    payload_recv_init.recv_initial_metadata.recv_initial_metadata =
        &server_recv_init_md;
    GRPC_CLOSURE_INIT(&recv_init_md_ready, OnRecvInitialMetadataReady, this,
                      nullptr);
    payload_recv_init.recv_initial_metadata.recv_initial_metadata_ready =
        &recv_init_md_ready;
    start_recv_init = {top, &batch_recv_init};
    GRPC_CLOSURE_INIT(&start_recv_init_closure, DoStartBatch, &start_recv_init,
                      nullptr);
    RunOnCombiner(&start_recv_init_closure);
  }

  void DeliverInitialMetadata(absl::string_view path = "/test/method") {
    mock.recv_initial_metadata->Set(HttpPathMetadata(),
                                    Slice::FromCopiedString(path));
    RunOnCombiner(mock.recv_initial_metadata_ready);
  }

  void SendTrailingMetadata(grpc_status_code status = GRPC_STATUS_OK) {
    batch_send_trail.payload = &payload_send_trail;
    batch_send_trail.send_trailing_metadata = true;
    server_send_trail_md.Set(GrpcStatusMetadata(), status);
    payload_send_trail.send_trailing_metadata.send_trailing_metadata =
        &server_send_trail_md;
    GRPC_CLOSURE_INIT(&send_trail_complete, OnSendTrailingComplete, this,
                      nullptr);
    batch_send_trail.on_complete = &send_trail_complete;
    start_send_trail = {top, &batch_send_trail};
    GRPC_CLOSURE_INIT(&start_send_trail_closure, DoStartBatch,
                      &start_send_trail, nullptr);
    RunOnCombiner(&start_send_trail_closure);
  }

  bool half_close_observed() const { return controller.half_close_observed; }
  bool recv_initial_metadata_ready_called() const {
    return recv_init_md_ready_called;
  }
  bool send_trailing_complete_called() const {
    return send_trail_complete_called;
  }

 private:
  static void OnRecvInitialMetadataReady(void* arg, grpc_error_handle) {
    auto* s = static_cast<FakeServerCallStack*>(arg);
    s->recv_init_md_ready_called = true;
    GRPC_CALL_COMBINER_STOP(&s->call_combiner,
                            "server:recv_initial_metadata_ready");
  }

  static void OnSendTrailingComplete(void* arg, grpc_error_handle) {
    auto* s = static_cast<FakeServerCallStack*>(arg);
    s->send_trail_complete_called = true;
    GRPC_CALL_COMBINER_STOP(&s->call_combiner, "server:send_trailing_complete");
  }

  CallCombiner call_combiner;
  RefCountedPtr<Arena> arena;
  MockTransportFilter::State mock;
  Controller controller;

  grpc_channel_stack* channel_stack;
  grpc_call_stack* call_stack;
  grpc_call_element* top;

  bool recv_init_md_ready_called = false;
  bool send_trail_complete_called = false;

  grpc_metadata_batch server_recv_init_md;
  grpc_closure recv_init_md_ready;
  grpc_transport_stream_op_batch batch_recv_init;
  grpc_transport_stream_op_batch_payload payload_recv_init{};
  StartBatchCtx start_recv_init;
  grpc_closure start_recv_init_closure;

  grpc_metadata_batch server_send_trail_md;
  grpc_closure send_trail_complete;
  grpc_transport_stream_op_batch batch_send_trail;
  grpc_transport_stream_op_batch_payload payload_send_trail{};
  StartBatchCtx start_send_trail;
  grpc_closure start_send_trail_closure;
};

// Verifies that when the server ends an RPC with OK status while idle (no
// longer reading messages), the inbound messages pipe is cleanly closed inside
// WakeInsideCombiner under an active Activity context, delivering EOF
// (client half-close) to filters observing client-to-server messages.
TEST(RecvMessageFilterBypassTest,
     ServerEndsRpcOkClosesInboundPipeWithHalfCloseInCombiner) {
  if (!IsPromiseFilterServerHalfCloseEnabled()) {
    GTEST_SKIP() << "Test fail without experiment";
  }
  ExecCtx exec_ctx;
  FakeServerCallStack env;
  // 1) Start recv_initial_metadata batch on the server call stack.
  env.StartRecvInitialMetadata();
  // 2) Transport delivers initial metadata, starting the server filter promise.
  env.DeliverInitialMetadata();
  EXPECT_TRUE(env.recv_initial_metadata_ready_called());
  EXPECT_FALSE(env.half_close_observed());
  // 3) Server completes the RPC by sending OK trailing metadata while idle
  // (not reading messages). This must trigger CloseInboundPipe() inside
  // WakeInsideCombiner with an active Activity context, causing the filter's
  // Next() reader to wake up and observe clean EOF (half-close).
  env.SendTrailingMetadata(GRPC_STATUS_OK);
  // Verify that the filter observed half-close and the trailing metadata
  // batch completed.
  EXPECT_TRUE(env.half_close_observed());
  EXPECT_TRUE(env.send_trailing_complete_called());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_core::ForceEnableExperiment("recv_message_filter_bypass_fix", true);
  grpc_core::ForceEnableExperiment("promise_filter_server_half_close", true);
  grpc_core::ForceEnableExperiment("recv_message_cancelled_status_fix", true);
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
