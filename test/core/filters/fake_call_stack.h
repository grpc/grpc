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

// A small harness for driving a v1 (filter stack) call by hand:
// a mock terminal transport that parks the recv ops until the test completes
// them, application side callbacks that record what they saw, and a wrapper
// around gRPC's verbose C-style call stack lifecycle setup.

#ifndef GRPC_TEST_CORE_FILTERS_FAKE_CALL_STACK_H
#define GRPC_TEST_CORE_FILTERS_FAKE_CALL_STACK_H

#include <grpc/support/alloc.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {

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

inline const grpc_channel_filter MockTransportFilter::kFilter = {
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

inline void DoStartBatch(void* arg, grpc_error_handle) {
  auto* c = static_cast<StartBatchCtx*>(arg);
  c->elem->filter->start_transport_stream_op_batch(c->elem, c->batch);
}

// A helper class that initializes a mock call stack, channel stack, call
// combiner, arena allocator, and App/Transport callbacks. This acts as a
// wrapper around gRPC's verbose C-style v1 call stack lifecycle setup.
//
// Batches are issued by the test (rather than by the constructor) so that the
// ordering between them can be varied.
class FakeCallStack {
 public:
  // `extra_channel_args` is merged into the preconditioned channel args, and
  // can be used to pass test specific objects down to the filters under test.
  explicit FakeCallStack(std::vector<FilterAndConfig> filters,
                         ChannelArgs extra_channel_args = ChannelArgs()) {
    mock.call_combiner = &call_combiner;
    auto channel_args = CoreConfiguration::Get()
                            .channel_args_preconditioning()
                            .PreconditionChannelArgs(nullptr)
                            .SetObject(&mock)
                            .UnionWith(std::move(extra_channel_args));
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
  }

  ~FakeCallStack() {
    GRPC_CALL_STACK_UNREF(call_stack, "done");
    ExecCtx::Get()->Flush();
    GRPC_CHANNEL_STACK_UNREF(channel_stack, "done");
  }

  // send_initial_metadata + recv_initial_metadata: this is what starts a
  // promise based filter's call promise.
  void StartInitialMetadataBatch() {
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
  }

  void StartRecvMessageBatch() {
    batch_b.payload = &payload_b;
    batch_b.recv_message = true;
    payload_b.recv_message.recv_message = &recv_message;
    payload_b.recv_message.flags = &recv_message_flags;
    payload_b.recv_message.recv_message_ready = &msg_ready;
    start_b = {top, &batch_b};
    GRPC_CLOSURE_INIT(&start_b_closure, DoStartBatch, &start_b, nullptr);
    RunOnCombiner(&start_b_closure);
  }

  void StartRecvTrailingMetadataBatch() {
    batch_c.payload = &payload_c;
    batch_c.recv_trailing_metadata = true;
    payload_c.recv_trailing_metadata.recv_trailing_metadata = &recv_trail_md;
    payload_c.recv_trailing_metadata.recv_trailing_metadata_ready =
        &trail_ready;
    start_c = {top, &batch_c};
    GRPC_CLOSURE_INIT(&start_c_closure, DoStartBatch, &start_c, nullptr);
    RunOnCombiner(&start_c_closure);
  }

  // The usual ordering: initial metadata, then recv_message, then
  // recv_trailing_metadata.
  void StartStandardBatches() {
    StartInitialMetadataBatch();
    StartRecvMessageBatch();
    StartRecvTrailingMetadataBatch();
  }

  void CancelStream(absl::Status error) {
    batch_cancel.payload = &payload_cancel;
    batch_cancel.cancel_stream = true;
    payload_cancel.cancel_stream.cancel_error = std::move(error);
    start_cancel = {top, &batch_cancel};
    GRPC_CLOSURE_INIT(&start_cancel_closure, DoStartBatch, &start_cancel,
                      nullptr);
    RunOnCombiner(&start_cancel_closure);
  }

  void RunOnCombiner(grpc_closure* c) {
    GRPC_CALL_COMBINER_START(&call_combiner, c, absl::OkStatus(), "test");
    ExecCtx::Get()->Flush();
  }

  CallCombiner call_combiner;
  RefCountedPtr<Arena> arena;
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

  grpc_transport_stream_op_batch batch_cancel;
  grpc_transport_stream_op_batch_payload payload_cancel{};
  StartBatchCtx start_cancel;
  grpc_closure start_cancel_closure;
};

}  // namespace testing
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_FILTERS_FAKE_CALL_STACK_H
