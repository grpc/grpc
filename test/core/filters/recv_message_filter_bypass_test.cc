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
#include <grpc/support/alloc.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/core/call/message.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

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

// Encapsulates the mock transport filter implementation and its call state.
class MockTransportFilter {
 public:
  struct State {
    struct RawPointerChannelArgTag {};
    static absl::string_view ChannelArgName() {
      return "grpc.test.mock_transport";
    }

    CallCombiner* call_combiner = nullptr;

    grpc_metadata_batch* recv_initial_metadata = nullptr;
    grpc_closure* recv_initial_metadata_ready = nullptr;

    std::optional<SliceBuffer>* recv_message = nullptr;
    uint32_t* recv_message_flags = nullptr;
    grpc_closure* recv_message_ready = nullptr;

    grpc_metadata_batch* recv_trailing_metadata = nullptr;
    grpc_closure* recv_trailing_metadata_ready = nullptr;
  };

  static const grpc_channel_filter kFilter;

 private:
  static void StartBatch(grpc_call_element* elem,
                         grpc_transport_stream_op_batch* op) {
    auto* mock_transport = *static_cast<State**>(elem->channel_data);
    if (op->recv_initial_metadata) {
      mock_transport->recv_initial_metadata =
          op->payload->recv_initial_metadata.recv_initial_metadata;
      mock_transport->recv_initial_metadata_ready =
          op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    }
    if (op->recv_message) {
      mock_transport->recv_message = op->payload->recv_message.recv_message;
      mock_transport->recv_message_flags = op->payload->recv_message.flags;
      mock_transport->recv_message_ready =
          op->payload->recv_message.recv_message_ready;
    }
    if (op->recv_trailing_metadata) {
      mock_transport->recv_trailing_metadata =
          op->payload->recv_trailing_metadata.recv_trailing_metadata;
      mock_transport->recv_trailing_metadata_ready =
          op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    }
    // The mock has no async sends, so complete any non-recv work immediately.
    if (op->on_complete != nullptr) {
      GRPC_CALL_COMBINER_START(mock_transport->call_combiner, op->on_complete,
                               absl::OkStatus(), "mock_on_complete");
    }
    // As the terminal transport, relinquish the call combiner that was passed
    // down with this batch (mirrors connected_channel.cc).
    GRPC_CALL_COMBINER_STOP(mock_transport->call_combiner,
                            "mock passed batch to transport");
  }

  static void StartTransportOp(grpc_channel_element*, grpc_transport_op* op) {
    if (op->on_consumed != nullptr) {
      ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
    }
  }

  static grpc_error_handle InitCallElem(grpc_call_element*,
                                        const grpc_call_element_args*) {
    return absl::OkStatus();
  }
  static void DestroyCallElem(grpc_call_element*, const grpc_call_final_info*,
                              grpc_closure*) {}
  static grpc_error_handle InitChannelElem(grpc_channel_element* elem,
                                           grpc_channel_element_args* args) {
    *static_cast<State**>(elem->channel_data) =
        args->channel_args.GetObject<State>();
    return absl::OkStatus();
  }
  static void DestroyChannelElem(grpc_channel_element*) {}
};

const grpc_channel_filter MockTransportFilter::kFilter = {
    MockTransportFilter::StartBatch,
    MockTransportFilter::StartTransportOp,
    0,  // sizeof_call_data
    MockTransportFilter::InitCallElem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    MockTransportFilter::DestroyCallElem,
    sizeof(MockTransportFilter::State*),  // sizeof_channel_data
    MockTransportFilter::InitChannelElem,
    grpc_channel_stack_no_post_init,
    MockTransportFilter::DestroyChannelElem,
    grpc_channel_next_get_info,
    GRPC_UNIQUE_TYPE_NAME_HERE("mock_transport"),
};

// Encapsulates the application-side callbacks and state.
class AppCallbacks {
 public:
  struct State {
    CallCombiner* combiner = nullptr;
    bool init_md_ready_called = false;
    bool msg_ready_called = false;
    bool trailing_ready_called = false;
    std::optional<SliceBuffer>* recv_message = nullptr;
    std::string captured_payload;
    absl::Status msg_status;
  };

  static void OnRecvInitialMetadataReady(void* arg, grpc_error_handle) {
    auto* ctx = static_cast<State*>(arg);
    ctx->init_md_ready_called = true;
    GRPC_CALL_COMBINER_STOP(ctx->combiner, "app:recv_initial_metadata_ready");
  }

  static void OnRecvMessageReady(void* arg, grpc_error_handle error) {
    auto* ctx = static_cast<State*>(arg);
    ctx->msg_ready_called = true;
    ctx->msg_status = error;
    if (ctx->recv_message->has_value()) {
      ctx->captured_payload = (*ctx->recv_message)->JoinIntoString();
    }
    GRPC_CALL_COMBINER_STOP(ctx->combiner, "app:recv_message_ready");
  }

  static void OnRecvTrailingMetadataReady(void* arg, grpc_error_handle) {
    auto* ctx = static_cast<State*>(arg);
    ctx->trailing_ready_called = true;
    GRPC_CALL_COMBINER_STOP(ctx->combiner, "app:recv_trailing_metadata_ready");
  }

  static void OnComplete(void* arg, grpc_error_handle) {
    GRPC_CALL_COMBINER_STOP(static_cast<CallCombiner*>(arg), "app:on_complete");
  }
};

struct StartBatchCtx {
  grpc_call_element* elem = nullptr;
  grpc_transport_stream_op_batch* batch = nullptr;
};
void DoStartBatch(void* arg, grpc_error_handle) {
  auto* c = static_cast<StartBatchCtx*>(arg);
  c->elem->filter->start_transport_stream_op_batch(c->elem, c->batch);
}

// A helper class that initializes a mock call stack, channel stack, call
// combiner, arena allocator, and App/Transport callbacks. This acts as a
// wrapper around gRPC's verbose C-style v1 call stack lifecycle setup.
class FakeCallStack {
 public:
  explicit FakeCallStack(std::vector<FilterAndConfig> filters,
                         MetadataStallController* control = nullptr) {
    control_ = control;
    mock.call_combiner = &call_combiner;
    auto channel_args = CoreConfiguration::Get()
                            .channel_args_preconditioning()
                            .PreconditionChannelArgs(nullptr)
                            .SetObject(&mock);
    if (control != nullptr) {
      channel_args = channel_args.SetObject(control);
    }
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
    app.combiner = &call_combiner;
    app.recv_message = &recv_message;

    GRPC_CLOSURE_INIT(&init_md_ready, AppCallbacks::OnRecvInitialMetadataReady,
                      &app, nullptr);
    GRPC_CLOSURE_INIT(&msg_ready, AppCallbacks::OnRecvMessageReady, &app,
                      nullptr);
    GRPC_CLOSURE_INIT(&trail_ready, AppCallbacks::OnRecvTrailingMetadataReady,
                      &app, nullptr);
    GRPC_CLOSURE_INIT(&on_complete, AppCallbacks::OnComplete, &call_combiner,
                      nullptr);

    // ---- Batch A: send_initial_metadata + recv_initial_metadata ----
    batch_a.payload = &payload_a;
    batch_a.send_initial_metadata = true;
    payload_a.send_initial_metadata.send_initial_metadata = &client_md;
    batch_a.recv_initial_metadata = true;
    payload_a.recv_initial_metadata.recv_initial_metadata = &recv_init_md;
    payload_a.recv_initial_metadata.recv_initial_metadata_ready =
        &init_md_ready;
    batch_a.on_complete = &on_complete;
    start_a = {top, &batch_a};
    GRPC_CLOSURE_INIT(&start_a_closure, DoStartBatch, &start_a, nullptr);
    RunOnCombiner(&start_a_closure);

    // ---- Batch B: recv_message ----
    batch_b.payload = &payload_b;
    batch_b.recv_message = true;
    payload_b.recv_message.recv_message = &recv_message;
    payload_b.recv_message.flags = &recv_message_flags;
    payload_b.recv_message.recv_message_ready = &msg_ready;
    start_b = {top, &batch_b};
    GRPC_CLOSURE_INIT(&start_b_closure, DoStartBatch, &start_b, nullptr);
    RunOnCombiner(&start_b_closure);

    // ---- Batch C: recv_trailing_metadata ----
    batch_c.payload = &payload_c;
    batch_c.recv_trailing_metadata = true;
    payload_c.recv_trailing_metadata.recv_trailing_metadata = &recv_trail_md;
    payload_c.recv_trailing_metadata.recv_trailing_metadata_ready =
        &trail_ready;
    start_c = {top, &batch_c};
    GRPC_CLOSURE_INIT(&start_c_closure, DoStartBatch, &start_c, nullptr);
    RunOnCombiner(&start_c_closure);
  }

  ~FakeCallStack() {
    // Reset wakers to prevent call stack from outliving the arena.
    if (control_ != nullptr) {
      control_->init_md_waker = Waker();
      control_->immediate_response_waker = Waker();
    }
    GRPC_CALL_STACK_UNREF(call_stack, "done");
    ExecCtx::Get()->Flush();
    GRPC_CHANNEL_STACK_UNREF(channel_stack, "done");
  }

  void RunOnCombiner(grpc_closure* c) {
    GRPC_CALL_COMBINER_START(&call_combiner, c, absl::OkStatus(), "test");
    ExecCtx::Get()->Flush();
  }

  CallCombiner call_combiner;
  RefCountedPtr<Arena> arena;
  MetadataStallController* control_ = nullptr;
  MockTransportFilter::State mock;
  AppCallbacks::State app;

  grpc_channel_stack* channel_stack;
  grpc_call_stack* call_stack;
  grpc_call_element* top;

  // Metadata / messages
  grpc_metadata_batch client_md;
  grpc_metadata_batch recv_init_md;
  grpc_metadata_batch recv_trail_md;
  std::optional<SliceBuffer> recv_message;
  uint32_t recv_message_flags = 0;

  // Closures
  grpc_closure init_md_ready;
  grpc_closure msg_ready;
  grpc_closure trail_ready;
  grpc_closure on_complete;

  // Batch structures
  grpc_transport_stream_op_batch batch_a;
  grpc_transport_stream_op_batch_payload payload_a{};
  StartBatchCtx start_a;
  grpc_closure start_a_closure;

  grpc_transport_stream_op_batch batch_b;
  grpc_transport_stream_op_batch_payload payload_b{};
  StartBatchCtx start_b;
  grpc_closure start_b_closure;

  grpc_transport_stream_op_batch batch_c;
  grpc_transport_stream_op_batch_payload payload_c{};
  StartBatchCtx start_c;
  grpc_closure start_c_closure;
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
  FakeCallStack env(
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
  FakeCallStack env(
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
  FakeCallStack env({{&kFilterA, nullptr},
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
  FakeCallStack env(
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
  grpc_transport_stream_op_batch batch_cancel;
  grpc_transport_stream_op_batch_payload payload_cancel{};
  batch_cancel.payload = &payload_cancel;
  batch_cancel.cancel_stream = true;
  payload_cancel.cancel_stream.cancel_error = absl::CancelledError();
  StartBatchCtx start_cancel{env.top, &batch_cancel};
  grpc_closure start_cancel_closure;
  GRPC_CLOSURE_INIT(&start_cancel_closure, DoStartBatch, &start_cancel,
                    nullptr);
  env.RunOnCombiner(&start_cancel_closure);
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
  FakeCallStack env(
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
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
