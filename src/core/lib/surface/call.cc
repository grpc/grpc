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

#include "src/core/lib/surface/call.h"

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
#include <atomic>
#include <cstdint>
#include <memory>
#include <new>
#include <queue>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "src/core/call/call_finalization.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/call/status_util.h"
#include "src/core/channelz/channelz.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/all_ok.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call_test_only.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/server/server_interface.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/alloc.h"
#include "src/core/util/bitset.h"
#include "src/core/util/cpp_impl_of.h"
#include "src/core/util/crash.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/match.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/sync.h"
#include "src/core/util/time_precise.h"
#include "src/core/util/useful.h"

namespace grpc_core {

// Alias to make this type available in Call implementation without a grpc_core
// prefix.
using GrpcClosure = Closure;

///////////////////////////////////////////////////////////////////////////////
// Call

Call::Call(bool is_client, Timestamp send_deadline, RefCountedPtr<Arena> arena)
    : arena_(std::move(arena)),
      send_deadline_(send_deadline),
      is_client_(is_client) {
  DCHECK_NE(arena_.get(), nullptr);
  DCHECK_NE(arena_->GetContext<grpc_event_engine::experimental::EventEngine>(),
            nullptr);
  arena_->SetContext<Call>(this);
}

Call::ParentCall* Call::GetOrCreateParentCall() {
  ParentCall* p = parent_call_.load(std::memory_order_acquire);
  if (p == nullptr) {
    p = arena()->New<ParentCall>();
    ParentCall* expected = nullptr;
    if (!parent_call_.compare_exchange_strong(expected, p,
                                              std::memory_order_release,
                                              std::memory_order_relaxed)) {
      p->~ParentCall();
      p = expected;
    }
  }
  return p;
}

Call::ParentCall* Call::parent_call() {
  return parent_call_.load(std::memory_order_acquire);
}

absl::Status Call::InitParent(Call* parent, uint32_t propagation_mask) {
  child_ = arena()->New<ChildCall>(parent);

  parent->InternalRef("child");
  CHECK(is_client_);
  CHECK(!parent->is_client_);

  if (propagation_mask & GRPC_PROPAGATE_DEADLINE) {
    send_deadline_ = std::min(send_deadline_, parent->send_deadline_);
  }
  // for now GRPC_PROPAGATE_TRACING_CONTEXT *MUST* be passed with
  // GRPC_PROPAGATE_STATS_CONTEXT
  // TODO(ctiller): This should change to use the appropriate census start_op
  // call.
  if (propagation_mask & GRPC_PROPAGATE_CENSUS_TRACING_CONTEXT) {
    if (0 == (propagation_mask & GRPC_PROPAGATE_CENSUS_STATS_CONTEXT)) {
      return absl::UnknownError(
          "Census tracing propagation requested without Census context "
          "propagation");
    }
    arena()->SetContext<census_context>(
        parent->arena()->GetContext<census_context>());
  } else if (propagation_mask & GRPC_PROPAGATE_CENSUS_STATS_CONTEXT) {
    return absl::UnknownError(
        "Census context propagation requested without Census tracing "
        "propagation");
  }
  if (propagation_mask & GRPC_PROPAGATE_CANCELLATION) {
    cancellation_is_inherited_ = true;
  }
  return absl::OkStatus();
}

void Call::PublishToParent(Call* parent) {
  ChildCall* cc = child_;
  ParentCall* pc = parent->GetOrCreateParentCall();
  MutexLock lock(&pc->child_list_mu);
  if (pc->first_child == nullptr) {
    pc->first_child = this;
    cc->sibling_next = cc->sibling_prev = this;
  } else {
    cc->sibling_next = pc->first_child;
    cc->sibling_prev = pc->first_child->child_->sibling_prev;
    cc->sibling_next->child_->sibling_prev =
        cc->sibling_prev->child_->sibling_next = this;
  }
  if (parent->Completed()) {
    CancelWithError(absl::CancelledError("CANCELLED"));
  }
}

void Call::MaybeUnpublishFromParent() {
  ChildCall* cc = child_;
  if (cc == nullptr) return;

  ParentCall* pc = cc->parent->parent_call();
  {
    MutexLock lock(&pc->child_list_mu);
    if (this == pc->first_child) {
      pc->first_child = cc->sibling_next;
      if (this == pc->first_child) {
        pc->first_child = nullptr;
      }
    }
    cc->sibling_prev->child_->sibling_next = cc->sibling_next;
    cc->sibling_next->child_->sibling_prev = cc->sibling_prev;
  }
  cc->parent->InternalUnref("child");
}

void Call::CancelWithStatus(grpc_status_code status, const char* description) {
  if (!IsErrorFlattenEnabled()) {
    CancelWithError(grpc_error_set_int(
        grpc_error_set_str(
            absl::Status(static_cast<absl::StatusCode>(status), description),
            StatusStrProperty::kGrpcMessage, description),
        StatusIntProperty::kRpcStatus, status));
    return;
  }
  if (status == GRPC_STATUS_OK) {
    VLOG(2) << "CancelWithStatus() called with OK status, using UNKNOWN";
    status = GRPC_STATUS_UNKNOWN;
  }
  CancelWithError(
      absl::Status(static_cast<absl::StatusCode>(status), description));
}

void Call::PropagateCancellationToChildren() {
  ParentCall* pc = parent_call();
  if (pc != nullptr) {
    Call* child;
    MutexLock lock(&pc->child_list_mu);
    child = pc->first_child;
    if (child != nullptr) {
      do {
        Call* next_child_call = child->child_->sibling_next;
        if (child->cancellation_is_inherited_) {
          child->InternalRef("propagate_cancel");
          child->CancelWithError(absl::CancelledError("CANCELLED"));
          child->InternalUnref("propagate_cancel");
        }
        child = next_child_call;
      } while (child != pc->first_child);
    }
  }
}

void Call::PrepareOutgoingInitialMetadata(const grpc_op& op,
                                          grpc_metadata_batch& md) {
  // TODO(juanlishen): If the user has already specified a compression
  // algorithm by setting the initial metadata with key of
  // GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY, we shouldn't override that
  // with the compression algorithm mapped from compression level.
  // process compression level
  grpc_compression_level effective_compression_level = GRPC_COMPRESS_LEVEL_NONE;
  bool level_set = false;
  if (op.data.send_initial_metadata.maybe_compression_level.is_set) {
    effective_compression_level =
        op.data.send_initial_metadata.maybe_compression_level.level;
    level_set = true;
  } else {
    const grpc_compression_options copts = compression_options();
    if (copts.default_level.is_set) {
      level_set = true;
      effective_compression_level = copts.default_level.level;
    }
  }
  // Currently, only server side supports compression level setting.
  if (level_set && !is_client()) {
    const grpc_compression_algorithm calgo =
        encodings_accepted_by_peer().CompressionAlgorithmForLevel(
            effective_compression_level);
    // The following metadata will be checked and removed by the message
    // compression filter. It will be used as the call's compression
    // algorithm.
    md.Set(GrpcInternalEncodingRequest(), calgo);
  }
  // Ignore any te metadata key value pairs specified.
  md.Remove(TeMetadata());
  // Should never come from applications
  md.Remove(GrpcLbClientStatsMetadata());
}

void Call::ProcessIncomingInitialMetadata(grpc_metadata_batch& md) {
  Slice* peer_string = md.get_pointer(PeerString());
  if (peer_string != nullptr) SetPeerString(peer_string->Ref());

  SetIncomingCompressionAlgorithm(
      md.Take(GrpcEncodingMetadata()).value_or(GRPC_COMPRESS_NONE));
  encodings_accepted_by_peer_ =
      md.Take(GrpcAcceptEncodingMetadata())
          .value_or(CompressionAlgorithmSet{GRPC_COMPRESS_NONE});

  const grpc_compression_options copts = compression_options();
  const grpc_compression_algorithm compression_algorithm =
      incoming_compression_algorithm();
  if (GPR_UNLIKELY(
          !CompressionAlgorithmSet::FromUint32(copts.enabled_algorithms_bitset)
               .IsSet(compression_algorithm))) {
    // check if algorithm is supported by current channel config
    HandleCompressionAlgorithmDisabled(compression_algorithm);
  }
  // GRPC_COMPRESS_NONE is always set.
  DCHECK(encodings_accepted_by_peer_.IsSet(GRPC_COMPRESS_NONE));
  if (GPR_UNLIKELY(!encodings_accepted_by_peer_.IsSet(compression_algorithm))) {
    if (GRPC_TRACE_FLAG_ENABLED(compression)) {
      HandleCompressionAlgorithmNotAccepted(compression_algorithm);
    }
  }
}

void Call::HandleCompressionAlgorithmNotAccepted(
    grpc_compression_algorithm compression_algorithm) {
  const char* algo_name = nullptr;
  grpc_compression_algorithm_name(compression_algorithm, &algo_name);
  LOG(ERROR) << "Compression algorithm ('" << algo_name
             << "') not present in the accepted encodings ("
             << encodings_accepted_by_peer_.ToString() << ")";
}

void Call::HandleCompressionAlgorithmDisabled(
    grpc_compression_algorithm compression_algorithm) {
  const char* algo_name = nullptr;
  grpc_compression_algorithm_name(compression_algorithm, &algo_name);
  std::string error_msg =
      absl::StrFormat("Compression algorithm '%s' is disabled.", algo_name);
  LOG(ERROR) << error_msg;
  CancelWithError(grpc_error_set_int(absl::UnimplementedError(error_msg),
                                     StatusIntProperty::kRpcStatus,
                                     GRPC_STATUS_UNIMPLEMENTED));
}

void Call::UpdateDeadline(Timestamp deadline) {
  ReleasableMutexLock lock(&deadline_mu_);
  GRPC_TRACE_LOG(call, INFO)
      << "[call " << this << "] UpdateDeadline from=" << deadline_.ToString()
      << " to=" << deadline.ToString();
  if (deadline >= deadline_) return;
  if (deadline < Timestamp::Now()) {
    lock.Release();
    CancelWithError(grpc_error_set_int(
        absl::DeadlineExceededError("Deadline Exceeded"),
        StatusIntProperty::kRpcStatus, GRPC_STATUS_DEADLINE_EXCEEDED));
    return;
  }
  auto* event_engine =
      arena_->GetContext<grpc_event_engine::experimental::EventEngine>();
  if (deadline_ != Timestamp::InfFuture()) {
    if (!event_engine->Cancel(deadline_task_)) return;
  } else {
    InternalRef("deadline");
  }
  deadline_ = deadline;
  deadline_task_ = event_engine->RunAfter(deadline - Timestamp::Now(), this);
}

void Call::ResetDeadline() {
  {
    MutexLock lock(&deadline_mu_);
    if (deadline_ == Timestamp::InfFuture()) return;
    if (!arena_->GetContext<grpc_event_engine::experimental::EventEngine>()
             ->Cancel(deadline_task_)) {
      return;
    }
    deadline_ = Timestamp::InfFuture();
  }
  InternalUnref("deadline[reset]");
}

void Call::Run() {
  ExecCtx exec_ctx;
  GRPC_TRACE_LOG(call, INFO)
      << "call deadline expired "
      << GRPC_DUMP_ARGS(Timestamp::Now(), send_deadline_);
  CancelWithError(grpc_error_set_int(
      absl::DeadlineExceededError("Deadline Exceeded"),
      StatusIntProperty::kRpcStatus, GRPC_STATUS_DEADLINE_EXCEEDED));
  InternalUnref("deadline[run]");
}

}  // namespace grpc_core

///////////////////////////////////////////////////////////////////////////////
// C-based API

void* grpc_call_arena_alloc(grpc_call* call, size_t size) {
  grpc_core::ExecCtx exec_ctx;
  return grpc_core::Call::FromC(call)->arena()->Alloc(size);
}

void grpc_call_set_completion_queue(grpc_call* call,
                                    grpc_completion_queue* cq) {
  grpc_core::Call::FromC(call)->SetCompletionQueue(cq);
}

void grpc_call_ref(grpc_call* c) { grpc_core::Call::FromC(c)->ExternalRef(); }

void grpc_call_unref(grpc_call* c) {
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Call::FromC(c)->ExternalUnref();
}

char* grpc_call_get_peer(grpc_call* call) {
  return grpc_core::Call::FromC(call)->GetPeer();
}

grpc_call_error grpc_call_cancel(grpc_call* call, void* reserved) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_call_cancel(call=" << call << ", reserved=" << reserved << ")";
  CHECK_EQ(reserved, nullptr);
  if (call == nullptr) {
    return GRPC_CALL_ERROR;
  }
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Call::FromC(call)->CancelWithError(
      absl::CancelledError("CANCELLED"));
  return GRPC_CALL_OK;
}

grpc_call_error grpc_call_cancel_with_status(grpc_call* c,
                                             grpc_status_code status,
                                             const char* description,
                                             void* reserved) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_call_cancel_with_status(c=" << c << ", status=" << (int)status
      << ", description=" << description << ", reserved=" << reserved << ")";
  CHECK_EQ(reserved, nullptr);
  if (c == nullptr) {
    return GRPC_CALL_ERROR;
  }
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Call::FromC(c)->CancelWithStatus(status, description);
  return GRPC_CALL_OK;
}

void grpc_call_cancel_internal(grpc_call* call) {
  grpc_core::Call::FromC(call)->CancelWithError(
      absl::CancelledError("CANCELLED"));
}

grpc_compression_algorithm grpc_call_test_only_get_compression_algorithm(
    grpc_call* call) {
  return grpc_core::Call::FromC(call)->incoming_compression_algorithm();
}

uint32_t grpc_call_test_only_get_message_flags(grpc_call* call) {
  return grpc_core::Call::FromC(call)->test_only_message_flags();
}

uint32_t grpc_call_test_only_get_encodings_accepted_by_peer(grpc_call* call) {
  return grpc_core::Call::FromC(call)
      ->encodings_accepted_by_peer()
      .ToLegacyBitmask();
}

grpc_core::Arena* grpc_call_get_arena(grpc_call* call) {
  return grpc_core::Call::FromC(call)->arena();
}

grpc_call_stack* grpc_call_get_call_stack(grpc_call* call) {
  return grpc_core::Call::FromC(call)->call_stack();
}

grpc_call_error grpc_call_start_batch(grpc_call* call, const grpc_op* ops,
                                      size_t nops, void* tag, void* reserved) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_call_start_batch(call=" << call << ", ops=" << ops
      << ", nops=" << (unsigned long)nops << ", tag=" << tag
      << ", reserved=" << reserved << ")";

  if (reserved != nullptr || call == nullptr) {
    return GRPC_CALL_ERROR;
  } else {
    grpc_core::ExecCtx exec_ctx;
    return grpc_core::Call::FromC(call)->StartBatch(ops, nops, tag, false);
  }
}

grpc_call_error grpc_call_start_batch_and_execute(grpc_call* call,
                                                  const grpc_op* ops,
                                                  size_t nops,
                                                  grpc_closure* closure) {
  return grpc_core::Call::FromC(call)->StartBatch(ops, nops, closure, true);
}

void grpc_call_tracer_set(grpc_call* call,
                          grpc_core::ClientCallTracer* tracer) {
  grpc_core::Arena* arena = grpc_call_get_arena(call);
  return arena->SetContext<grpc_core::CallTracerAnnotationInterface>(tracer);
}

void grpc_call_tracer_set_and_manage(grpc_call* call,
                                     grpc_core::ClientCallTracer* tracer) {
  grpc_core::Arena* arena = grpc_call_get_arena(call);
  arena->ManagedNew<ClientCallTracerWrapper>(tracer);
  return arena->SetContext<grpc_core::CallTracerAnnotationInterface>(tracer);
}

void* grpc_call_tracer_get(grpc_call* call) {
  grpc_core::Arena* arena = grpc_call_get_arena(call);
  auto* call_tracer =
      arena->GetContext<grpc_core::CallTracerAnnotationInterface>();
  return call_tracer;
}

uint8_t grpc_call_is_client(grpc_call* call) {
  return grpc_core::Call::FromC(call)->is_client();
}

grpc_compression_algorithm grpc_call_compression_for_level(
    grpc_call* call, grpc_compression_level level) {
  return grpc_core::Call::FromC(call)
      ->encodings_accepted_by_peer()
      .CompressionAlgorithmForLevel(level);
}

bool grpc_call_is_trailers_only(const grpc_call* call) {
  return grpc_core::Call::FromC(call)->is_trailers_only();
}

int grpc_call_failed_before_recv_message(const grpc_call* c) {
  return grpc_core::Call::FromC(c)->failed_before_recv_message();
}

absl::string_view grpc_call_server_authority(const grpc_call* call) {
  return grpc_core::Call::FromC(call)->GetServerAuthority();
}

const char* grpc_call_error_to_string(grpc_call_error error) {
  switch (error) {
    case GRPC_CALL_ERROR:
      return "GRPC_CALL_ERROR";
    case GRPC_CALL_ERROR_ALREADY_ACCEPTED:
      return "GRPC_CALL_ERROR_ALREADY_ACCEPTED";
    case GRPC_CALL_ERROR_ALREADY_FINISHED:
      return "GRPC_CALL_ERROR_ALREADY_FINISHED";
    case GRPC_CALL_ERROR_ALREADY_INVOKED:
      return "GRPC_CALL_ERROR_ALREADY_INVOKED";
    case GRPC_CALL_ERROR_BATCH_TOO_BIG:
      return "GRPC_CALL_ERROR_BATCH_TOO_BIG";
    case GRPC_CALL_ERROR_INVALID_FLAGS:
      return "GRPC_CALL_ERROR_INVALID_FLAGS";
    case GRPC_CALL_ERROR_INVALID_MESSAGE:
      return "GRPC_CALL_ERROR_INVALID_MESSAGE";
    case GRPC_CALL_ERROR_INVALID_METADATA:
      return "GRPC_CALL_ERROR_INVALID_METADATA";
    case GRPC_CALL_ERROR_NOT_INVOKED:
      return "GRPC_CALL_ERROR_NOT_INVOKED";
    case GRPC_CALL_ERROR_NOT_ON_CLIENT:
      return "GRPC_CALL_ERROR_NOT_ON_CLIENT";
    case GRPC_CALL_ERROR_NOT_ON_SERVER:
      return "GRPC_CALL_ERROR_NOT_ON_SERVER";
    case GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE:
      return "GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE";
    case GRPC_CALL_ERROR_PAYLOAD_TYPE_MISMATCH:
      return "GRPC_CALL_ERROR_PAYLOAD_TYPE_MISMATCH";
    case GRPC_CALL_ERROR_TOO_MANY_OPERATIONS:
      return "GRPC_CALL_ERROR_TOO_MANY_OPERATIONS";
    case GRPC_CALL_ERROR_COMPLETION_QUEUE_SHUTDOWN:
      return "GRPC_CALL_ERROR_COMPLETION_QUEUE_SHUTDOWN";
    case GRPC_CALL_OK:
      return "GRPC_CALL_OK";
  }
  GPR_UNREACHABLE_CODE(return "GRPC_CALL_ERROR_UNKNOW");
}

void grpc_call_run_in_event_engine(const grpc_call* call,
                                   absl::AnyInvocable<void()> cb) {
  grpc_core::Call::FromC(call)
      ->arena()
      ->GetContext<grpc_event_engine::experimental::EventEngine>()
      ->Run(std::move(cb));
}
