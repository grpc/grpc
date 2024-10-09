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

#include "src/core/lib/surface/filter_stack_call.h"

#include <grpc/byte_buffer.h>
#include <grpc/compression.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/call.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/channelz/channelz.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call_utils.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/server/server_interface.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/alloc.h"
#include "src/core/util/crash.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/time_precise.h"

namespace grpc_core {

// Alias to make this type available in Call implementation without a grpc_core
// prefix.
using GrpcClosure = Closure;

FilterStackCall::FilterStackCall(RefCountedPtr<Arena> arena,
                                 const grpc_call_create_args& args)
    : Call(args.server_transport_data == nullptr, args.send_deadline,
           std::move(arena)),
      channel_(args.channel->RefAsSubclass<Channel>()),
      cq_(args.cq),
      stream_op_payload_{} {}

grpc_error_handle FilterStackCall::Create(grpc_call_create_args* args,
                                          grpc_call** out_call) {
  Channel* channel = args->channel.get();

  auto add_init_error = [](grpc_error_handle* composite,
                           grpc_error_handle new_err) {
    if (new_err.ok()) return;
    if (composite->ok()) {
      *composite = GRPC_ERROR_CREATE("Call creation failed");
    }
    *composite = grpc_error_add_child(*composite, new_err);
  };

  FilterStackCall* call;
  grpc_error_handle error;
  grpc_channel_stack* channel_stack = channel->channel_stack();
  size_t call_alloc_size =
      GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(FilterStackCall)) +
      channel_stack->call_stack_size;

  RefCountedPtr<Arena> arena = channel->call_arena_allocator()->MakeArena();
  arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      args->channel->event_engine());
  call = new (arena->Alloc(call_alloc_size)) FilterStackCall(arena, *args);
  DCHECK(FromC(call->c_ptr()) == call);
  DCHECK(FromCallStack(call->call_stack()) == call);
  *out_call = call->c_ptr();
  grpc_slice path = grpc_empty_slice();
  ScopedContext ctx(call);
  if (call->is_client()) {
    call->final_op_.client.status_details = nullptr;
    call->final_op_.client.status = nullptr;
    call->final_op_.client.error_string = nullptr;
    global_stats().IncrementClientCallsCreated();
    path = CSliceRef(args->path->c_slice());
    call->send_initial_metadata_.Set(HttpPathMetadata(),
                                     std::move(*args->path));
    if (args->authority.has_value()) {
      call->send_initial_metadata_.Set(HttpAuthorityMetadata(),
                                       std::move(*args->authority));
    }
    call->send_initial_metadata_.Set(
        GrpcRegisteredMethod(), reinterpret_cast<void*>(static_cast<uintptr_t>(
                                    args->registered_method)));
    channel_stack->stats_plugin_group->AddClientCallTracers(
        Slice(CSliceRef(path)), args->registered_method, arena.get());
  } else {
    global_stats().IncrementServerCallsCreated();
    call->final_op_.server.cancelled = nullptr;
    call->final_op_.server.core_server = args->server;
    // TODO(yashykt): In the future, we want to also enable stats and trace
    // collecting from when the call is created at the transport. The idea is
    // that the transport would create the call tracer and pass it in as part of
    // the metadata.
    // TODO(yijiem): OpenCensus and internal Census is still using this way to
    // set server call tracer. We need to refactor them to stats plugins
    // (including removing the client channel filters).
    if (args->server != nullptr &&
        args->server->server_call_tracer_factory() != nullptr) {
      auto* server_call_tracer =
          args->server->server_call_tracer_factory()->CreateNewServerCallTracer(
              arena.get(), args->server->channel_args());
      if (server_call_tracer != nullptr) {
        // Note that we are setting both
        // GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE and
        // GRPC_CONTEXT_CALL_TRACER as a matter of convenience. In the future
        // promise-based world, we would just a single tracer object for each
        // stack (call, subchannel_call, server_call.)
        arena->SetContext<CallTracerAnnotationInterface>(server_call_tracer);
        arena->SetContext<CallTracerInterface>(server_call_tracer);
      }
    }
    channel_stack->stats_plugin_group->AddServerCallTracers(arena.get());
  }

  Call* parent = Call::FromC(args->parent);
  if (parent != nullptr) {
    add_init_error(&error, absl_status_to_grpc_error(call->InitParent(
                               parent, args->propagation_mask)));
  }
  // initial refcount dropped by grpc_call_unref
  grpc_call_element_args call_args = {
      call->call_stack(),   args->server_transport_data, path,
      call->start_time(),   call->send_deadline(),       call->arena(),
      &call->call_combiner_};
  add_init_error(&error, grpc_call_stack_init(channel_stack, 1, DestroyCall,
                                              call, &call_args));
  // Publish this call to parent only after the call stack has been initialized.
  if (parent != nullptr) {
    call->PublishToParent(parent);
  }

  if (!error.ok()) {
    call->CancelWithError(error);
  }
  if (args->cq != nullptr) {
    CHECK(args->pollset_set_alternative == nullptr)
        << "Only one of 'cq' and 'pollset_set_alternative' should be "
           "non-nullptr.";
    GRPC_CQ_INTERNAL_REF(args->cq, "bind");
    call->pollent_ =
        grpc_polling_entity_create_from_pollset(grpc_cq_pollset(args->cq));
  }
  if (args->pollset_set_alternative != nullptr) {
    call->pollent_ = grpc_polling_entity_create_from_pollset_set(
        args->pollset_set_alternative);
  }
  if (!grpc_polling_entity_is_empty(&call->pollent_)) {
    grpc_call_stack_set_pollset_or_pollset_set(call->call_stack(),
                                               &call->pollent_);
  }

  if (call->is_client()) {
    channelz::ChannelNode* channelz_channel = channel->channelz_node();
    if (channelz_channel != nullptr) {
      channelz_channel->RecordCallStarted();
    }
  } else if (call->final_op_.server.core_server != nullptr) {
    channelz::ServerNode* channelz_node =
        call->final_op_.server.core_server->channelz_node();
    if (channelz_node != nullptr) {
      channelz_node->RecordCallStarted();
    }
  }

  if (args->send_deadline != Timestamp::InfFuture()) {
    call->UpdateDeadline(args->send_deadline);
  }

  CSliceUnref(path);

  return error;
}

void FilterStackCall::SetCompletionQueue(grpc_completion_queue* cq) {
  CHECK(cq);

  if (grpc_polling_entity_pollset_set(&pollent_) != nullptr) {
    Crash("A pollset_set is already registered for this call.");
  }
  cq_ = cq;
  GRPC_CQ_INTERNAL_REF(cq, "bind");
  pollent_ = grpc_polling_entity_create_from_pollset(grpc_cq_pollset(cq));
  grpc_call_stack_set_pollset_or_pollset_set(call_stack(), &pollent_);
}

void FilterStackCall::ReleaseCall(void* call, grpc_error_handle /*error*/) {
  static_cast<FilterStackCall*>(call)->DeleteThis();
}

void FilterStackCall::DestroyCall(void* call, grpc_error_handle /*error*/) {
  auto* c = static_cast<FilterStackCall*>(call);
  c->recv_initial_metadata_.Clear();
  c->recv_trailing_metadata_.Clear();
  c->receiving_slice_buffer_.reset();
  ParentCall* pc = c->parent_call();
  if (pc != nullptr) {
    pc->~ParentCall();
  }
  if (c->cq_) {
    GRPC_CQ_INTERNAL_UNREF(c->cq_, "bind");
  }

  grpc_error_handle status_error = c->status_error_.get();
  grpc_error_get_status(status_error, c->send_deadline(),
                        &c->final_info_.final_status, nullptr, nullptr,
                        &(c->final_info_.error_string));
  c->status_error_.set(absl::OkStatus());
  c->final_info_.stats.latency =
      gpr_cycle_counter_sub(gpr_get_cycle_counter(), c->start_time());
  grpc_call_stack_destroy(c->call_stack(), &c->final_info_,
                          GRPC_CLOSURE_INIT(&c->release_call_, ReleaseCall, c,
                                            grpc_schedule_on_exec_ctx));
}

void FilterStackCall::ExternalUnref() {
  if (GPR_LIKELY(!ext_ref_.Unref())) return;

  ApplicationCallbackExecCtx callback_exec_ctx;
  ExecCtx exec_ctx;

  GRPC_TRACE_LOG(api, INFO) << "grpc_call_unref(c=" << this << ")";

  MaybeUnpublishFromParent();

  CHECK(!destroy_called_);
  destroy_called_ = true;
  bool cancel = gpr_atm_acq_load(&received_final_op_atm_) == 0;
  if (cancel) {
    CancelWithError(absl::CancelledError());
  } else {
    // Unset the call combiner cancellation closure.  This has the
    // effect of scheduling the previously set cancellation closure, if
    // any, so that it can release any internal references it may be
    // holding to the call stack.
    call_combiner_.SetNotifyOnCancel(nullptr);
  }
  InternalUnref("destroy");
}

// start_batch_closure points to a caller-allocated closure to be used
// for entering the call combiner.
void FilterStackCall::ExecuteBatch(grpc_transport_stream_op_batch* batch,
                                   grpc_closure* start_batch_closure) {
  // This is called via the call combiner to start sending a batch down
  // the filter stack.
  auto execute_batch_in_call_combiner = [](void* arg, grpc_error_handle) {
    grpc_transport_stream_op_batch* batch =
        static_cast<grpc_transport_stream_op_batch*>(arg);
    auto* call =
        static_cast<FilterStackCall*>(batch->handler_private.extra_arg);
    grpc_call_element* elem = call->call_elem(0);
    GRPC_TRACE_LOG(channel, INFO)
        << "OP[" << elem->filter->name << ":" << elem
        << "]: " << grpc_transport_stream_op_batch_string(batch, false);
    elem->filter->start_transport_stream_op_batch(elem, batch);
  };
  batch->handler_private.extra_arg = this;
  GRPC_CLOSURE_INIT(start_batch_closure, execute_batch_in_call_combiner, batch,
                    grpc_schedule_on_exec_ctx);
  GRPC_CALL_COMBINER_START(call_combiner(), start_batch_closure,
                           absl::OkStatus(), "executing batch");
}

namespace {
struct CancelState {
  FilterStackCall* call;
  grpc_closure start_batch;
  grpc_closure finish_batch;
};
}  // namespace

// The on_complete callback used when sending a cancel_stream batch down
// the filter stack.  Yields the call combiner when the batch is done.
static void done_termination(void* arg, grpc_error_handle /*error*/) {
  CancelState* state = static_cast<CancelState*>(arg);
  GRPC_CALL_COMBINER_STOP(state->call->call_combiner(),
                          "on_complete for cancel_stream op");
  state->call->InternalUnref("termination");
  delete state;
}

void FilterStackCall::CancelWithError(grpc_error_handle error) {
  if (!gpr_atm_rel_cas(&cancelled_with_error_, 0, 1)) {
    return;
  }
  GRPC_TRACE_LOG(call_error, INFO)
      << "CancelWithError " << (is_client() ? "CLI" : "SVR") << " "
      << StatusToString(error);
  ClearPeerString();
  InternalRef("termination");
  ResetDeadline();
  // Inform the call combiner of the cancellation, so that it can cancel
  // any in-flight asynchronous actions that may be holding the call
  // combiner.  This ensures that the cancel_stream batch can be sent
  // down the filter stack in a timely manner.
  call_combiner_.Cancel(error);
  CancelState* state = new CancelState;
  state->call = this;
  GRPC_CLOSURE_INIT(&state->finish_batch, done_termination, state,
                    grpc_schedule_on_exec_ctx);
  grpc_transport_stream_op_batch* op =
      grpc_make_transport_stream_op(&state->finish_batch);
  op->cancel_stream = true;
  op->payload->cancel_stream.cancel_error = error;
  ExecuteBatch(op, &state->start_batch);
}

void FilterStackCall::SetFinalStatus(grpc_error_handle error) {
  GRPC_TRACE_LOG(call_error, INFO)
      << "set_final_status " << (is_client() ? "CLI" : "SVR") << " "
      << StatusToString(error);
  ResetDeadline();
  if (is_client()) {
    std::string status_details;
    grpc_error_get_status(error, send_deadline(), final_op_.client.status,
                          &status_details, nullptr,
                          final_op_.client.error_string);
    *final_op_.client.status_details =
        grpc_slice_from_cpp_string(std::move(status_details));
    status_error_.set(error);
    channelz::ChannelNode* channelz_channel = channel()->channelz_node();
    if (channelz_channel != nullptr) {
      if (*final_op_.client.status != GRPC_STATUS_OK) {
        channelz_channel->RecordCallFailed();
      } else {
        channelz_channel->RecordCallSucceeded();
      }
    }
  } else {
    *final_op_.server.cancelled =
        !error.ok() || !sent_server_trailing_metadata_;
    channelz::ServerNode* channelz_node =
        final_op_.server.core_server->channelz_node();
    if (channelz_node != nullptr) {
      if (*final_op_.server.cancelled || !status_error_.ok()) {
        channelz_node->RecordCallFailed();
      } else {
        channelz_node->RecordCallSucceeded();
      }
    }
  }
}

bool FilterStackCall::PrepareApplicationMetadata(size_t count,
                                                 grpc_metadata* metadata,
                                                 bool is_trailing) {
  grpc_metadata_batch* batch =
      is_trailing ? &send_trailing_metadata_ : &send_initial_metadata_;
  for (size_t i = 0; i < count; i++) {
    grpc_metadata* md = &metadata[i];
    if (!GRPC_LOG_IF_ERROR("validate_metadata",
                           grpc_validate_header_key_is_legal(md->key))) {
      return false;
    } else if (!grpc_is_binary_header_internal(md->key) &&
               !GRPC_LOG_IF_ERROR(
                   "validate_metadata",
                   grpc_validate_header_nonbin_value_is_legal(md->value))) {
      return false;
    } else if (GRPC_SLICE_LENGTH(md->value) >= UINT32_MAX) {
      // HTTP2 hpack encoding has a maximum limit.
      return false;
    } else if (grpc_slice_str_cmp(md->key, "content-length") == 0) {
      // Filter "content-length metadata"
      continue;
    }
    batch->Append(StringViewFromSlice(md->key), Slice(CSliceRef(md->value)),
                  [md](absl::string_view error, const Slice& value) {
                    VLOG(2)
                        << "Append error: key=" << StringViewFromSlice(md->key)
                        << " error=" << error
                        << " value=" << value.as_string_view();
                  });
  }

  return true;
}

void FilterStackCall::PublishAppMetadata(grpc_metadata_batch* b,
                                         bool is_trailing) {
  if (b->count() == 0) return;
  if (!is_client() && is_trailing) return;
  if (is_trailing && buffered_metadata_[1] == nullptr) return;
  grpc_metadata_array* dest;
  dest = buffered_metadata_[is_trailing];
  if (dest->count + b->count() > dest->capacity) {
    dest->capacity =
        std::max(dest->capacity + b->count(), dest->capacity * 3 / 2);
    dest->metadata = static_cast<grpc_metadata*>(
        gpr_realloc(dest->metadata, sizeof(grpc_metadata) * dest->capacity));
  }
  PublishToAppEncoder encoder(dest, b, is_client());
  b->Encode(&encoder);
}

void FilterStackCall::RecvInitialFilter(grpc_metadata_batch* b) {
  ProcessIncomingInitialMetadata(*b);
  PublishAppMetadata(b, false);
}

void FilterStackCall::RecvTrailingFilter(grpc_metadata_batch* b,
                                         grpc_error_handle batch_error) {
  if (!batch_error.ok()) {
    SetFinalStatus(batch_error);
  } else {
    absl::optional<grpc_status_code> grpc_status =
        b->Take(GrpcStatusMetadata());
    if (grpc_status.has_value()) {
      grpc_status_code status_code = *grpc_status;
      grpc_error_handle error;
      if (status_code != GRPC_STATUS_OK) {
        Slice peer = GetPeerString();
        error = grpc_error_set_int(
            GRPC_ERROR_CREATE(absl::StrCat("Error received from peer ",
                                           peer.as_string_view())),
            StatusIntProperty::kRpcStatus, static_cast<intptr_t>(status_code));
      }
      auto grpc_message = b->Take(GrpcMessageMetadata());
      if (grpc_message.has_value()) {
        error = grpc_error_set_str(error, StatusStrProperty::kGrpcMessage,
                                   grpc_message->as_string_view());
      } else if (!error.ok()) {
        error = grpc_error_set_str(error, StatusStrProperty::kGrpcMessage, "");
      }
      SetFinalStatus(error);
    } else if (!is_client()) {
      SetFinalStatus(absl::OkStatus());
    } else {
      VLOG(2) << "Received trailing metadata with no error and no status";
      SetFinalStatus(grpc_error_set_int(GRPC_ERROR_CREATE("No status received"),
                                        StatusIntProperty::kRpcStatus,
                                        GRPC_STATUS_UNKNOWN));
    }
  }
  PublishAppMetadata(b, true);
}

namespace {
size_t BatchSlotForOp(grpc_op_type type) {
  switch (type) {
    case GRPC_OP_SEND_INITIAL_METADATA:
      return 0;
    case GRPC_OP_SEND_MESSAGE:
      return 1;
    case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
    case GRPC_OP_SEND_STATUS_FROM_SERVER:
      return 2;
    case GRPC_OP_RECV_INITIAL_METADATA:
      return 3;
    case GRPC_OP_RECV_MESSAGE:
      return 4;
    case GRPC_OP_RECV_CLOSE_ON_SERVER:
    case GRPC_OP_RECV_STATUS_ON_CLIENT:
      return 5;
  }
  GPR_UNREACHABLE_CODE(return 123456789);
}
}  // namespace

FilterStackCall::BatchControl* FilterStackCall::ReuseOrAllocateBatchControl(
    const grpc_op* ops) {
  size_t slot_idx = BatchSlotForOp(ops[0].op);
  BatchControl** pslot = &active_batches_[slot_idx];
  BatchControl* bctl;
  if (*pslot != nullptr) {
    bctl = *pslot;
    if (bctl->call_ != nullptr) {
      return nullptr;
    }
    bctl->~BatchControl();
    bctl->op_ = {};
    new (&bctl->batch_error_) AtomicError();
  } else {
    bctl = arena()->New<BatchControl>();
    *pslot = bctl;
  }
  bctl->call_ = this;
  bctl->call_tracer_ = arena()->GetContext<CallTracerAnnotationInterface>();
  bctl->op_.payload = &stream_op_payload_;
  return bctl;
}

void FilterStackCall::BatchControl::PostCompletion() {
  FilterStackCall* call = call_;
  grpc_error_handle error = batch_error_.get();

  // On the client side, if final call status is already known (i.e if this op
  // includes recv_trailing_metadata) and if the call status is known to be
  // OK, then disregard the batch error to ensure call->receiving_buffer_ is
  // not cleared.
  if (op_.recv_trailing_metadata && call->is_client() &&
      call->status_error_.ok()) {
    error = absl::OkStatus();
  }

  GRPC_TRACE_VLOG(call, 2) << "tag:" << completion_data_.notify_tag.tag
                           << " batch_error=" << error << " op:"
                           << grpc_transport_stream_op_batch_string(&op_,
                                                                    false);

  if (op_.send_initial_metadata) {
    call->send_initial_metadata_.Clear();
  }
  if (op_.send_message) {
    if (op_.payload->send_message.stream_write_closed && error.ok()) {
      error = grpc_error_add_child(
          error, GRPC_ERROR_CREATE(
                     "Attempt to send message after stream was closed."));
    }
    call->sending_message_ = false;
    call->send_slice_buffer_.Clear();
  }
  if (op_.send_trailing_metadata) {
    call->send_trailing_metadata_.Clear();
  }

  if (!error.ok() && op_.recv_message && *call->receiving_buffer_ != nullptr) {
    grpc_byte_buffer_destroy(*call->receiving_buffer_);
    *call->receiving_buffer_ = nullptr;
  }
  if (op_.recv_trailing_metadata) {
    // propagate cancellation to any interested children
    gpr_atm_rel_store(&call->received_final_op_atm_, 1);
    call->PropagateCancellationToChildren();
    error = absl::OkStatus();
  }
  batch_error_.set(absl::OkStatus());

  if (completion_data_.notify_tag.is_closure) {
    call_ = nullptr;
    GrpcClosure::Run(
        DEBUG_LOCATION,
        static_cast<grpc_closure*>(completion_data_.notify_tag.tag), error);
    call->InternalUnref("completion");
  } else {
    grpc_cq_end_op(
        call->cq_, completion_data_.notify_tag.tag, error,
        [](void* user_data, grpc_cq_completion* /*storage*/) {
          BatchControl* bctl = static_cast<BatchControl*>(user_data);
          Call* call = bctl->call_;
          bctl->call_ = nullptr;
          call->InternalUnref("completion");
        },
        this, &completion_data_.cq_completion);
  }
}

void FilterStackCall::BatchControl::FinishStep(PendingOp op) {
  if (GPR_UNLIKELY(completed_batch_step(op))) {
    PostCompletion();
  }
}

void FilterStackCall::BatchControl::ProcessDataAfterMetadata() {
  FilterStackCall* call = call_;
  if (!call->receiving_slice_buffer_.has_value()) {
    *call->receiving_buffer_ = nullptr;
    call->receiving_message_ = false;
    FinishStep(PendingOp::kRecvMessage);
  } else {
    call->test_only_last_message_flags_ = call->receiving_stream_flags_;
    if ((call->receiving_stream_flags_ & GRPC_WRITE_INTERNAL_COMPRESS) &&
        (call->incoming_compression_algorithm() != GRPC_COMPRESS_NONE)) {
      *call->receiving_buffer_ = grpc_raw_compressed_byte_buffer_create(
          nullptr, 0, call->incoming_compression_algorithm());
    } else {
      *call->receiving_buffer_ = grpc_raw_byte_buffer_create(nullptr, 0);
    }
    grpc_slice_buffer_move_into(
        call->receiving_slice_buffer_->c_slice_buffer(),
        &(*call->receiving_buffer_)->data.raw.slice_buffer);
    call->receiving_message_ = false;
    call->receiving_slice_buffer_.reset();
    FinishStep(PendingOp::kRecvMessage);
  }
}

void FilterStackCall::BatchControl::ReceivingStreamReady(
    grpc_error_handle error) {
  GRPC_TRACE_VLOG(call, 2) << "tag:" << completion_data_.notify_tag.tag
                           << " ReceivingStreamReady error=" << error
                           << " receiving_slice_buffer.has_value="
                           << call_->receiving_slice_buffer_.has_value()
                           << " recv_state="
                           << gpr_atm_no_barrier_load(&call_->recv_state_);
  FilterStackCall* call = call_;
  if (!error.ok()) {
    call->receiving_slice_buffer_.reset();
    if (batch_error_.ok()) {
      batch_error_.set(error);
    }
    call->CancelWithError(error);
  }
  // If recv_state is kRecvNone, we will save the batch_control
  // object with rel_cas, and will not use it after the cas. Its corresponding
  // acq_load is in receiving_initial_metadata_ready()
  if (!error.ok() || !call->receiving_slice_buffer_.has_value() ||
      !gpr_atm_rel_cas(&call->recv_state_, kRecvNone,
                       reinterpret_cast<gpr_atm>(this))) {
    ProcessDataAfterMetadata();
  }
}

void FilterStackCall::BatchControl::ReceivingInitialMetadataReady(
    grpc_error_handle error) {
  FilterStackCall* call = call_;

  GRPC_CALL_COMBINER_STOP(call->call_combiner(), "recv_initial_metadata_ready");

  if (error.ok()) {
    grpc_metadata_batch* md = &call->recv_initial_metadata_;
    call->RecvInitialFilter(md);

    absl::optional<Timestamp> deadline = md->get(GrpcTimeoutMetadata());
    if (deadline.has_value() && !call->is_client()) {
      call_->set_send_deadline(*deadline);
    }
  } else {
    if (batch_error_.ok()) {
      batch_error_.set(error);
    }
    call->CancelWithError(error);
  }

  grpc_closure* saved_rsr_closure = nullptr;
  while (true) {
    gpr_atm rsr_bctlp = gpr_atm_acq_load(&call->recv_state_);
    // Should only receive initial metadata once
    CHECK_NE(rsr_bctlp, 1);
    if (rsr_bctlp == 0) {
      // We haven't seen initial metadata and messages before, thus initial
      // metadata is received first.
      // no_barrier_cas is used, as this function won't access the batch_control
      // object saved by receiving_stream_ready() if the initial metadata is
      // received first.
      if (gpr_atm_no_barrier_cas(&call->recv_state_, kRecvNone,
                                 kRecvInitialMetadataFirst)) {
        break;
      }
    } else {
      // Already received messages
      saved_rsr_closure = GRPC_CLOSURE_CREATE(
          [](void* bctl, grpc_error_handle error) {
            static_cast<BatchControl*>(bctl)->ReceivingStreamReady(error);
          },
          reinterpret_cast<BatchControl*>(rsr_bctlp),
          grpc_schedule_on_exec_ctx);
      // No need to modify recv_state
      break;
    }
  }
  if (saved_rsr_closure != nullptr) {
    GrpcClosure::Run(DEBUG_LOCATION, saved_rsr_closure, error);
  }

  FinishStep(PendingOp::kRecvInitialMetadata);
}

void FilterStackCall::BatchControl::ReceivingTrailingMetadataReady(
    grpc_error_handle error) {
  GRPC_CALL_COMBINER_STOP(call_->call_combiner(),
                          "recv_trailing_metadata_ready");
  grpc_metadata_batch* md = &call_->recv_trailing_metadata_;
  call_->RecvTrailingFilter(md, error);
  FinishStep(PendingOp::kRecvTrailingMetadata);
}

void FilterStackCall::BatchControl::FinishBatch(grpc_error_handle error) {
  GRPC_CALL_COMBINER_STOP(call_->call_combiner(), "on_complete");
  if (batch_error_.ok()) {
    batch_error_.set(error);
  }
  if (!error.ok()) {
    call_->CancelWithError(error);
  }
  FinishStep(PendingOp::kSends);
}

grpc_call_error FilterStackCall::StartBatch(const grpc_op* ops, size_t nops,
                                            void* notify_tag,
                                            bool is_notify_tag_closure) {
  GRPC_LATENT_SEE_INNER_SCOPE("FilterStackCall::StartBatch");

  size_t i;
  const grpc_op* op;
  BatchControl* bctl;
  grpc_call_error error = GRPC_CALL_OK;
  grpc_transport_stream_op_batch* stream_op;
  grpc_transport_stream_op_batch_payload* stream_op_payload;
  uint32_t seen_ops = 0;
  intptr_t pending_ops = 0;

  for (i = 0; i < nops; i++) {
    if (seen_ops & (1u << ops[i].op)) {
      return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
    }
    seen_ops |= (1u << ops[i].op);
  }

  if (!is_client() &&
      (seen_ops & (1u << GRPC_OP_SEND_STATUS_FROM_SERVER)) != 0 &&
      (seen_ops & (1u << GRPC_OP_RECV_MESSAGE)) != 0) {
    LOG(ERROR) << "******************* SEND_STATUS WITH RECV_MESSAGE "
                  "*******************";
    return GRPC_CALL_ERROR;
  }

  GRPC_CALL_LOG_BATCH(ops, nops);

  if (nops == 0) {
    EndOpImmediately(cq_, notify_tag, is_notify_tag_closure);
    error = GRPC_CALL_OK;
    goto done;
  }

  bctl = ReuseOrAllocateBatchControl(ops);
  if (bctl == nullptr) {
    return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
  }
  bctl->completion_data_.notify_tag.tag = notify_tag;
  bctl->completion_data_.notify_tag.is_closure =
      static_cast<uint8_t>(is_notify_tag_closure != 0);

  stream_op = &bctl->op_;
  stream_op_payload = &stream_op_payload_;

  // rewrite batch ops into a transport op
  for (i = 0; i < nops; i++) {
    op = &ops[i];
    if (op->reserved != nullptr) {
      error = GRPC_CALL_ERROR;
      goto done_with_error;
    }
    switch (op->op) {
      case GRPC_OP_SEND_INITIAL_METADATA: {
        // Flag validation: currently allow no flags
        if (!AreInitialMetadataFlagsValid(op->flags)) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (sent_initial_metadata_) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        if (op->data.send_initial_metadata.count > INT_MAX) {
          error = GRPC_CALL_ERROR_INVALID_METADATA;
          goto done_with_error;
        }
        stream_op->send_initial_metadata = true;
        sent_initial_metadata_ = true;
        if (!PrepareApplicationMetadata(op->data.send_initial_metadata.count,
                                        op->data.send_initial_metadata.metadata,
                                        false)) {
          error = GRPC_CALL_ERROR_INVALID_METADATA;
          goto done_with_error;
        }
        PrepareOutgoingInitialMetadata(*op, send_initial_metadata_);
        // TODO(ctiller): just make these the same variable?
        if (is_client() && send_deadline() != Timestamp::InfFuture()) {
          send_initial_metadata_.Set(GrpcTimeoutMetadata(), send_deadline());
        }
        if (is_client()) {
          send_initial_metadata_.Set(
              WaitForReady(),
              WaitForReady::ValueType{
                  (op->flags & GRPC_INITIAL_METADATA_WAIT_FOR_READY) != 0,
                  (op->flags &
                   GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET) != 0});
        }
        stream_op_payload->send_initial_metadata.send_initial_metadata =
            &send_initial_metadata_;
        pending_ops |= PendingOpMask(PendingOp::kSends);
        break;
      }
      case GRPC_OP_SEND_MESSAGE: {
        if (!AreWriteFlagsValid(op->flags)) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (op->data.send_message.send_message == nullptr) {
          error = GRPC_CALL_ERROR_INVALID_MESSAGE;
          goto done_with_error;
        }
        if (sending_message_) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        uint32_t flags = op->flags;
        // If the outgoing buffer is already compressed, mark it as so in the
        // flags. These will be picked up by the compression filter and further
        // (wasteful) attempts at compression skipped.
        if (op->data.send_message.send_message->data.raw.compression >
            GRPC_COMPRESS_NONE) {
          flags |= GRPC_WRITE_INTERNAL_COMPRESS;
        }
        stream_op->send_message = true;
        sending_message_ = true;
        send_slice_buffer_.Clear();
        grpc_slice_buffer_move_into(
            &op->data.send_message.send_message->data.raw.slice_buffer,
            send_slice_buffer_.c_slice_buffer());
        stream_op_payload->send_message.flags = flags;
        stream_op_payload->send_message.send_message = &send_slice_buffer_;
        pending_ops |= PendingOpMask(PendingOp::kSends);
        break;
      }
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT: {
        // Flag validation: currently allow no flags
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (!is_client()) {
          error = GRPC_CALL_ERROR_NOT_ON_SERVER;
          goto done_with_error;
        }
        if (sent_final_op_) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        stream_op->send_trailing_metadata = true;
        sent_final_op_ = true;
        stream_op_payload->send_trailing_metadata.send_trailing_metadata =
            &send_trailing_metadata_;
        pending_ops |= PendingOpMask(PendingOp::kSends);
        break;
      }
      case GRPC_OP_SEND_STATUS_FROM_SERVER: {
        // Flag validation: currently allow no flags
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (is_client()) {
          error = GRPC_CALL_ERROR_NOT_ON_CLIENT;
          goto done_with_error;
        }
        if (sent_final_op_) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        if (op->data.send_status_from_server.trailing_metadata_count >
            INT_MAX) {
          error = GRPC_CALL_ERROR_INVALID_METADATA;
          goto done_with_error;
        }
        stream_op->send_trailing_metadata = true;
        sent_final_op_ = true;

        if (!PrepareApplicationMetadata(
                op->data.send_status_from_server.trailing_metadata_count,
                op->data.send_status_from_server.trailing_metadata, true)) {
          error = GRPC_CALL_ERROR_INVALID_METADATA;
          goto done_with_error;
        }

        grpc_error_handle status_error =
            op->data.send_status_from_server.status == GRPC_STATUS_OK
                ? absl::OkStatus()
                : grpc_error_set_int(
                      GRPC_ERROR_CREATE("Server returned error"),
                      StatusIntProperty::kRpcStatus,
                      static_cast<intptr_t>(
                          op->data.send_status_from_server.status));
        if (op->data.send_status_from_server.status_details != nullptr) {
          send_trailing_metadata_.Set(
              GrpcMessageMetadata(),
              Slice(grpc_slice_copy(
                  *op->data.send_status_from_server.status_details)));
          if (!status_error.ok()) {
            status_error = grpc_error_set_str(
                status_error, StatusStrProperty::kGrpcMessage,
                StringViewFromSlice(
                    *op->data.send_status_from_server.status_details));
          }
        }

        status_error_.set(status_error);

        send_trailing_metadata_.Set(GrpcStatusMetadata(),
                                    op->data.send_status_from_server.status);

        // Ignore any te metadata key value pairs specified.
        send_trailing_metadata_.Remove(TeMetadata());
        stream_op_payload->send_trailing_metadata.send_trailing_metadata =
            &send_trailing_metadata_;
        stream_op_payload->send_trailing_metadata.sent =
            &sent_server_trailing_metadata_;
        pending_ops |= PendingOpMask(PendingOp::kSends);
        break;
      }
      case GRPC_OP_RECV_INITIAL_METADATA: {
        // Flag validation: currently allow no flags
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (received_initial_metadata_) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        received_initial_metadata_ = true;
        buffered_metadata_[0] =
            op->data.recv_initial_metadata.recv_initial_metadata;
        GRPC_CLOSURE_INIT(
            &receiving_initial_metadata_ready_,
            [](void* bctl, grpc_error_handle error) {
              static_cast<BatchControl*>(bctl)->ReceivingInitialMetadataReady(
                  error);
            },
            bctl, grpc_schedule_on_exec_ctx);
        stream_op->recv_initial_metadata = true;
        stream_op_payload->recv_initial_metadata.recv_initial_metadata =
            &recv_initial_metadata_;
        stream_op_payload->recv_initial_metadata.recv_initial_metadata_ready =
            &receiving_initial_metadata_ready_;
        if (is_client()) {
          stream_op_payload->recv_initial_metadata.trailing_metadata_available =
              &is_trailers_only_;
        }
        pending_ops |= PendingOpMask(PendingOp::kRecvInitialMetadata);
        break;
      }
      case GRPC_OP_RECV_MESSAGE: {
        // Flag validation: currently allow no flags
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (receiving_message_) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        receiving_message_ = true;
        stream_op->recv_message = true;
        receiving_slice_buffer_.reset();
        receiving_buffer_ = op->data.recv_message.recv_message;
        stream_op_payload->recv_message.recv_message = &receiving_slice_buffer_;
        receiving_stream_flags_ = 0;
        stream_op_payload->recv_message.flags = &receiving_stream_flags_;
        stream_op_payload->recv_message.call_failed_before_recv_message =
            &call_failed_before_recv_message_;
        GRPC_CLOSURE_INIT(
            &receiving_stream_ready_,
            [](void* bctlp, grpc_error_handle error) {
              auto* bctl = static_cast<BatchControl*>(bctlp);
              auto* call = bctl->call_;
              //  Yields the call combiner before processing the received
              //  message.
              GRPC_CALL_COMBINER_STOP(call->call_combiner(),
                                      "recv_message_ready");
              bctl->ReceivingStreamReady(error);
            },
            bctl, grpc_schedule_on_exec_ctx);
        stream_op_payload->recv_message.recv_message_ready =
            &receiving_stream_ready_;
        pending_ops |= PendingOpMask(PendingOp::kRecvMessage);
        break;
      }
      case GRPC_OP_RECV_STATUS_ON_CLIENT: {
        // Flag validation: currently allow no flags
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (!is_client()) {
          error = GRPC_CALL_ERROR_NOT_ON_SERVER;
          goto done_with_error;
        }
        if (requested_final_op_) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        requested_final_op_ = true;
        buffered_metadata_[1] =
            op->data.recv_status_on_client.trailing_metadata;
        final_op_.client.status = op->data.recv_status_on_client.status;
        final_op_.client.status_details =
            op->data.recv_status_on_client.status_details;
        final_op_.client.error_string =
            op->data.recv_status_on_client.error_string;
        stream_op->recv_trailing_metadata = true;
        stream_op_payload->recv_trailing_metadata.recv_trailing_metadata =
            &recv_trailing_metadata_;
        stream_op_payload->recv_trailing_metadata.collect_stats =
            &final_info_.stats.transport_stream_stats;
        GRPC_CLOSURE_INIT(
            &receiving_trailing_metadata_ready_,
            [](void* bctl, grpc_error_handle error) {
              static_cast<BatchControl*>(bctl)->ReceivingTrailingMetadataReady(
                  error);
            },
            bctl, grpc_schedule_on_exec_ctx);
        stream_op_payload->recv_trailing_metadata.recv_trailing_metadata_ready =
            &receiving_trailing_metadata_ready_;
        pending_ops |= PendingOpMask(PendingOp::kRecvTrailingMetadata);
        break;
      }
      case GRPC_OP_RECV_CLOSE_ON_SERVER: {
        // Flag validation: currently allow no flags
        if (op->flags != 0) {
          error = GRPC_CALL_ERROR_INVALID_FLAGS;
          goto done_with_error;
        }
        if (is_client()) {
          error = GRPC_CALL_ERROR_NOT_ON_CLIENT;
          goto done_with_error;
        }
        if (requested_final_op_) {
          error = GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
          goto done_with_error;
        }
        requested_final_op_ = true;
        final_op_.server.cancelled = op->data.recv_close_on_server.cancelled;
        stream_op->recv_trailing_metadata = true;
        stream_op_payload->recv_trailing_metadata.recv_trailing_metadata =
            &recv_trailing_metadata_;
        stream_op_payload->recv_trailing_metadata.collect_stats =
            &final_info_.stats.transport_stream_stats;
        GRPC_CLOSURE_INIT(
            &receiving_trailing_metadata_ready_,
            [](void* bctl, grpc_error_handle error) {
              static_cast<BatchControl*>(bctl)->ReceivingTrailingMetadataReady(
                  error);
            },
            bctl, grpc_schedule_on_exec_ctx);
        stream_op_payload->recv_trailing_metadata.recv_trailing_metadata_ready =
            &receiving_trailing_metadata_ready_;
        pending_ops |= PendingOpMask(PendingOp::kRecvTrailingMetadata);
        break;
      }
    }
  }

  InternalRef("completion");
  if (!is_notify_tag_closure) {
    CHECK(grpc_cq_begin_op(cq_, notify_tag));
  }
  bctl->set_pending_ops(pending_ops);

  if (pending_ops & PendingOpMask(PendingOp::kSends)) {
    GRPC_CLOSURE_INIT(
        &bctl->finish_batch_,
        [](void* bctl, grpc_error_handle error) {
          static_cast<BatchControl*>(bctl)->FinishBatch(error);
        },
        bctl, grpc_schedule_on_exec_ctx);
    stream_op->on_complete = &bctl->finish_batch_;
  }

  GRPC_TRACE_VLOG(call, 2)
      << "BATCH:" << bctl << " START:" << PendingOpString(pending_ops)
      << " BATCH:" << grpc_transport_stream_op_batch_string(stream_op, false)
      << " (tag:" << bctl->completion_data_.notify_tag.tag << ")";
  ExecuteBatch(stream_op, &bctl->start_batch_);

done:
  return error;

done_with_error:
  // reverse any mutations that occurred
  if (stream_op->send_initial_metadata) {
    sent_initial_metadata_ = false;
    send_initial_metadata_.Clear();
  }
  if (stream_op->send_message) {
    sending_message_ = false;
  }
  if (stream_op->send_trailing_metadata) {
    sent_final_op_ = false;
    send_trailing_metadata_.Clear();
  }
  if (stream_op->recv_initial_metadata) {
    received_initial_metadata_ = false;
  }
  if (stream_op->recv_message) {
    receiving_message_ = false;
  }
  if (stream_op->recv_trailing_metadata) {
    requested_final_op_ = false;
  }
  goto done;
}

char* FilterStackCall::GetPeer() {
  Slice peer_slice = GetPeerString();
  if (!peer_slice.empty()) {
    absl::string_view peer_string_view = peer_slice.as_string_view();
    char* peer_string =
        static_cast<char*>(gpr_malloc(peer_string_view.size() + 1));
    memcpy(peer_string, peer_string_view.data(), peer_string_view.size());
    peer_string[peer_string_view.size()] = '\0';
    return peer_string;
  }
  char* peer_string = grpc_channel_get_target(channel_->c_ptr());
  if (peer_string != nullptr) return peer_string;
  return gpr_strdup("unknown");
}

}  // namespace grpc_core

grpc_error_handle grpc_call_create(grpc_call_create_args* args,
                                   grpc_call** out_call) {
  return grpc_core::FilterStackCall::Create(args, out_call);
}

grpc_call* grpc_call_from_top_element(grpc_call_element* surface_element) {
  return grpc_core::FilterStackCall::FromTopElem(surface_element)->c_ptr();
}
