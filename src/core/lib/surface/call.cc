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
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include "src/core/channelz/channelz.h"
#include "src/core/lib/channel/call_finalization.h"
#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/gprpp/cpp_impl_of.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
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
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call_test_only.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/surface/wait_for_cq_end_op.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/server/server_interface.h"
#include "src/core/util/alloc.h"
#include "src/core/util/time_precise.h"
#include "src/core/util/useful.h"

grpc_core::TraceFlag grpc_call_error_trace(false, "call_error");
grpc_core::TraceFlag grpc_compression_trace(false, "compression");
grpc_core::TraceFlag grpc_call_trace(false, "call");
grpc_core::DebugOnlyTraceFlag grpc_call_refcount_trace(false, "call_refcount");

namespace grpc_core {

// Alias to make this type available in Call implementation without a grpc_core
// prefix.
using GrpcClosure = Closure;

///////////////////////////////////////////////////////////////////////////////
// Call

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
    ContextSet(GRPC_CONTEXT_TRACING, parent->ContextGet(GRPC_CONTEXT_TRACING),
               nullptr);
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
    CancelWithError(absl::CancelledError());
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
  // copying 'description' is needed to ensure the grpc_call_cancel_with_status
  // guarantee that can be short-lived.
  // TODO(ctiller): change to
  // absl::Status(static_cast<absl::StatusCode>(status), description)
  // (ie remove the set_int, set_str).
  CancelWithError(grpc_error_set_int(
      grpc_error_set_str(
          absl::Status(static_cast<absl::StatusCode>(status), description),
          StatusStrProperty::kGrpcMessage, description),
      StatusIntProperty::kRpcStatus, status));
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
          child->CancelWithError(absl::CancelledError());
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
    if (GRPC_TRACE_FLAG_ENABLED(grpc_compression_trace)) {
      HandleCompressionAlgorithmNotAccepted(compression_algorithm);
    }
  }
}

void Call::HandleCompressionAlgorithmNotAccepted(
    grpc_compression_algorithm compression_algorithm) {
  const char* algo_name = nullptr;
  grpc_compression_algorithm_name(compression_algorithm, &algo_name);
  gpr_log(GPR_ERROR,
          "Compression algorithm ('%s') not present in the "
          "accepted encodings (%s)",
          algo_name,
          std::string(encodings_accepted_by_peer_.ToString()).c_str());
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
  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG, "[call %p] UpdateDeadline from=%s to=%s", this,
            deadline_.ToString().c_str(), deadline.ToString().c_str());
  }
  if (deadline >= deadline_) return;
  if (deadline < Timestamp::Now()) {
    lock.Release();
    CancelWithError(grpc_error_set_int(
        absl::DeadlineExceededError("Deadline Exceeded"),
        StatusIntProperty::kRpcStatus, GRPC_STATUS_DEADLINE_EXCEEDED));
    return;
  }
  if (deadline_ != Timestamp::InfFuture()) {
    if (!event_engine_->Cancel(deadline_task_)) return;
  } else {
    InternalRef("deadline");
  }
  deadline_ = deadline;
  deadline_task_ = event_engine_->RunAfter(deadline - Timestamp::Now(), this);
}

void Call::ResetDeadline() {
  {
    MutexLock lock(&deadline_mu_);
    if (deadline_ == Timestamp::InfFuture()) return;
    if (!event_engine_->Cancel(deadline_task_)) return;
    deadline_ = Timestamp::InfFuture();
  }
  InternalUnref("deadline[reset]");
}

void Call::Run() {
  ApplicationCallbackExecCtx callback_exec_ctx;
  ExecCtx exec_ctx;
  CancelWithError(grpc_error_set_int(
      absl::DeadlineExceededError("Deadline Exceeded"),
      StatusIntProperty::kRpcStatus, GRPC_STATUS_DEADLINE_EXCEEDED));
  InternalUnref("deadline[run]");
}

///////////////////////////////////////////////////////////////////////////////
// ChannelBasedCall
// TODO(ctiller): once we remove the v2 client code this can be folded into
// FilterStackCall

class ChannelBasedCall : public Call {
 protected:
  ChannelBasedCall(RefCountedPtr<Arena> arena, bool is_client,
                   Timestamp send_deadline, RefCountedPtr<Channel> channel)
      : Call(is_client, send_deadline, std::move(arena),
             channel->event_engine()),
        channel_(std::move(channel)) {}

  char* GetPeer() final {
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

  grpc_compression_options compression_options() override {
    return channel_->compression_options();
  }

  void DeleteThis() { this->~ChannelBasedCall(); }

  Channel* channel() const { return channel_.get(); }

 private:
  RefCountedPtr<Channel> channel_;
};

///////////////////////////////////////////////////////////////////////////////
// FilterStackCall
// To be removed once promise conversion is complete

class FilterStackCall final : public ChannelBasedCall {
 public:
  ~FilterStackCall() override {
    for (int i = 0; i < GRPC_CONTEXT_COUNT; ++i) {
      if (context_[i].destroy) {
        context_[i].destroy(context_[i].value);
      }
    }
    gpr_free(static_cast<void*>(const_cast<char*>(final_info_.error_string)));
  }

  bool Completed() override {
    return gpr_atm_acq_load(&received_final_op_atm_) != 0;
  }

  // TODO(ctiller): return absl::StatusOr<SomeSmartPointer<Call>>?
  static grpc_error_handle Create(grpc_call_create_args* args,
                                  grpc_call** out_call);

  static Call* FromTopElem(grpc_call_element* elem) {
    return FromCallStack(grpc_call_stack_from_top_element(elem));
  }

  grpc_call_stack* call_stack() override {
    return reinterpret_cast<grpc_call_stack*>(
        reinterpret_cast<char*>(this) +
        GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(*this)));
  }

  grpc_call_element* call_elem(size_t idx) {
    return grpc_call_stack_element(call_stack(), idx);
  }

  CallCombiner* call_combiner() { return &call_combiner_; }

  void CancelWithError(grpc_error_handle error) override;
  void SetCompletionQueue(grpc_completion_queue* cq) override;
  grpc_call_error StartBatch(const grpc_op* ops, size_t nops, void* notify_tag,
                             bool is_notify_tag_closure) override;
  void ExternalRef() override { ext_ref_.Ref(); }
  void ExternalUnref() override;
  void InternalRef(const char* reason) override {
    GRPC_CALL_STACK_REF(call_stack(), reason);
  }
  void InternalUnref(const char* reason) override {
    GRPC_CALL_STACK_UNREF(call_stack(), reason);
  }

  void ContextSet(grpc_context_index elem, void* value,
                  void (*destroy)(void* value)) override;
  void* ContextGet(grpc_context_index elem) const override {
    return context_[elem].value;
  }

  bool is_trailers_only() const override {
    bool result = is_trailers_only_;
    DCHECK(!result || recv_initial_metadata_.TransportSize() == 0);
    return result;
  }

  bool failed_before_recv_message() const override {
    return call_failed_before_recv_message_;
  }

  uint32_t test_only_message_flags() override {
    return test_only_last_message_flags_;
  }

  absl::string_view GetServerAuthority() const override {
    const Slice* authority_metadata =
        recv_initial_metadata_.get_pointer(HttpAuthorityMetadata());
    if (authority_metadata == nullptr) return "";
    return authority_metadata->as_string_view();
  }

  static size_t InitialSizeEstimate() {
    return sizeof(FilterStackCall) +
           sizeof(BatchControl) * kMaxConcurrentBatches;
  }

 private:
  class ScopedContext : public promise_detail::Context<Arena> {
   public:
    explicit ScopedContext(FilterStackCall* call)
        : promise_detail::Context<Arena>(call->arena()) {}
  };

  static constexpr gpr_atm kRecvNone = 0;
  static constexpr gpr_atm kRecvInitialMetadataFirst = 1;

  enum class PendingOp {
    kRecvMessage,
    kRecvInitialMetadata,
    kRecvTrailingMetadata,
    kSends
  };
  static intptr_t PendingOpMask(PendingOp op) {
    return static_cast<intptr_t>(1) << static_cast<intptr_t>(op);
  }
  static std::string PendingOpString(intptr_t pending_ops) {
    std::vector<absl::string_view> pending_op_strings;
    if (pending_ops & PendingOpMask(PendingOp::kRecvMessage)) {
      pending_op_strings.push_back("kRecvMessage");
    }
    if (pending_ops & PendingOpMask(PendingOp::kRecvInitialMetadata)) {
      pending_op_strings.push_back("kRecvInitialMetadata");
    }
    if (pending_ops & PendingOpMask(PendingOp::kRecvTrailingMetadata)) {
      pending_op_strings.push_back("kRecvTrailingMetadata");
    }
    if (pending_ops & PendingOpMask(PendingOp::kSends)) {
      pending_op_strings.push_back("kSends");
    }
    return absl::StrCat("{", absl::StrJoin(pending_op_strings, ","), "}");
  }
  struct BatchControl {
    FilterStackCall* call_ = nullptr;
    CallTracerAnnotationInterface* call_tracer_ = nullptr;
    grpc_transport_stream_op_batch op_;
    // Share memory for cq_completion and notify_tag as they are never needed
    // simultaneously. Each byte used in this data structure count as six bytes
    // per call, so any savings we can make are worthwhile,

    // We use notify_tag to determine whether or not to send notification to the
    // completion queue. Once we've made that determination, we can reuse the
    // memory for cq_completion.
    union {
      grpc_cq_completion cq_completion;
      struct {
        // Any given op indicates completion by either (a) calling a closure or
        // (b) sending a notification on the call's completion queue.  If
        // \a is_closure is true, \a tag indicates a closure to be invoked;
        // otherwise, \a tag indicates the tag to be used in the notification to
        // be sent to the completion queue.
        void* tag;
        bool is_closure;
      } notify_tag;
    } completion_data_;
    grpc_closure start_batch_;
    grpc_closure finish_batch_;
    std::atomic<intptr_t> ops_pending_{0};
    AtomicError batch_error_;
    void set_pending_ops(uintptr_t ops) {
      ops_pending_.store(ops, std::memory_order_release);
    }
    bool completed_batch_step(PendingOp op) {
      auto mask = PendingOpMask(op);
      auto r = ops_pending_.fetch_sub(mask, std::memory_order_acq_rel);
      if (grpc_call_trace.enabled()) {
        gpr_log(GPR_DEBUG, "BATCH:%p COMPLETE:%s REMAINING:%s (tag:%p)", this,
                PendingOpString(mask).c_str(),
                PendingOpString(r & ~mask).c_str(),
                completion_data_.notify_tag.tag);
      }
      CHECK_NE((r & mask), 0);
      return r == mask;
    }

    void PostCompletion();
    void FinishStep(PendingOp op);
    void ProcessDataAfterMetadata();
    void ReceivingStreamReady(grpc_error_handle error);
    void ReceivingInitialMetadataReady(grpc_error_handle error);
    void ReceivingTrailingMetadataReady(grpc_error_handle error);
    void FinishBatch(grpc_error_handle error);
  };

  FilterStackCall(RefCountedPtr<Arena> arena, const grpc_call_create_args& args)
      : ChannelBasedCall(
            std::move(arena), args.server_transport_data == nullptr,
            args.send_deadline, args.channel->RefAsSubclass<Channel>()),
        cq_(args.cq),
        stream_op_payload_(context_) {
    context_[GRPC_CONTEXT_CALL].value = this;
  }

  static void ReleaseCall(void* call, grpc_error_handle);
  static void DestroyCall(void* call, grpc_error_handle);

  static FilterStackCall* FromCallStack(grpc_call_stack* call_stack) {
    return reinterpret_cast<FilterStackCall*>(
        reinterpret_cast<char*>(call_stack) -
        GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(FilterStackCall)));
  }

  void ExecuteBatch(grpc_transport_stream_op_batch* batch,
                    grpc_closure* start_batch_closure);
  void SetFinalStatus(grpc_error_handle error);
  BatchControl* ReuseOrAllocateBatchControl(const grpc_op* ops);
  bool PrepareApplicationMetadata(size_t count, grpc_metadata* metadata,
                                  bool is_trailing);
  void PublishAppMetadata(grpc_metadata_batch* b, bool is_trailing);
  void RecvInitialFilter(grpc_metadata_batch* b);
  void RecvTrailingFilter(grpc_metadata_batch* b,
                          grpc_error_handle batch_error);

  grpc_compression_algorithm incoming_compression_algorithm() override {
    return incoming_compression_algorithm_;
  }
  void SetIncomingCompressionAlgorithm(
      grpc_compression_algorithm algorithm) override {
    incoming_compression_algorithm_ = algorithm;
  }

  RefCount ext_ref_;
  CallCombiner call_combiner_;
  grpc_completion_queue* cq_;
  grpc_polling_entity pollent_;

  /// has grpc_call_unref been called
  bool destroy_called_ = false;
  // Trailers-only response status
  bool is_trailers_only_ = false;
  /// which ops are in-flight
  bool sent_initial_metadata_ = false;
  bool sending_message_ = false;
  bool sent_final_op_ = false;
  bool received_initial_metadata_ = false;
  bool receiving_message_ = false;
  bool requested_final_op_ = false;
  gpr_atm received_final_op_atm_ = 0;

  BatchControl* active_batches_[kMaxConcurrentBatches] = {};
  grpc_transport_stream_op_batch_payload stream_op_payload_;

  // first idx: is_receiving, second idx: is_trailing
  grpc_metadata_batch send_initial_metadata_;
  grpc_metadata_batch send_trailing_metadata_;
  grpc_metadata_batch recv_initial_metadata_;
  grpc_metadata_batch recv_trailing_metadata_;

  // Buffered read metadata waiting to be returned to the application.
  // Element 0 is initial metadata, element 1 is trailing metadata.
  grpc_metadata_array* buffered_metadata_[2] = {};

  // Call data useful used for reporting. Only valid after the call has
  // completed
  grpc_call_final_info final_info_;

  // Contexts for various subsystems (security, tracing, ...).
  grpc_call_context_element context_[GRPC_CONTEXT_COUNT] = {};

  SliceBuffer send_slice_buffer_;
  absl::optional<SliceBuffer> receiving_slice_buffer_;
  uint32_t receiving_stream_flags_;
  uint32_t test_only_last_message_flags_ = 0;
  // Compression algorithm for *incoming* data
  grpc_compression_algorithm incoming_compression_algorithm_ =
      GRPC_COMPRESS_NONE;

  bool call_failed_before_recv_message_ = false;
  grpc_byte_buffer** receiving_buffer_ = nullptr;
  grpc_slice receiving_slice_ = grpc_empty_slice();
  grpc_closure receiving_stream_ready_;
  grpc_closure receiving_initial_metadata_ready_;
  grpc_closure receiving_trailing_metadata_ready_;
  // Status about operation of call
  bool sent_server_trailing_metadata_ = false;
  gpr_atm cancelled_with_error_ = 0;

  grpc_closure release_call_;

  union {
    struct {
      grpc_status_code* status;
      grpc_slice* status_details;
      const char** error_string;
    } client;
    struct {
      int* cancelled;
      // backpointer to owning server if this is a server side call.
      ServerInterface* core_server;
    } server;
  } final_op_;
  AtomicError status_error_;

  // recv_state can contain one of the following values:
  // RECV_NONE :                 :  no initial metadata and messages received
  // RECV_INITIAL_METADATA_FIRST :  received initial metadata first
  // a batch_control*            :  received messages first

  //             +------1------RECV_NONE------3-----+
  //             |                                  |
  //             |                                  |
  //             v                                  v
  // RECV_INITIAL_METADATA_FIRST        receiving_stream_ready_bctlp
  //       |           ^                      |           ^
  //       |           |                      |           |
  //       +-----2-----+                      +-----4-----+

  // For 1, 4: See receiving_initial_metadata_ready() function
  // For 2, 3: See receiving_stream_ready() function
  gpr_atm recv_state_ = 0;
};

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
        Slice(CSliceRef(path)), args->registered_method, call->context_);
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
        call->ContextSet(GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE,
                         server_call_tracer, nullptr);
        call->ContextSet(GRPC_CONTEXT_CALL_TRACER, server_call_tracer, nullptr);
      }
    }
    channel_stack->stats_plugin_group->AddServerCallTracers(call->context_);
  }

  Call* parent = Call::FromC(args->parent);
  if (parent != nullptr) {
    add_init_error(&error, absl_status_to_grpc_error(call->InitParent(
                               parent, args->propagation_mask)));
  }
  // initial refcount dropped by grpc_call_unref
  grpc_call_element_args call_args = {
      call->call_stack(), args->server_transport_data,
      call->context_,     path,
      call->start_time(), call->send_deadline(),
      call->arena(),      &call->call_combiner_};
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

  GRPC_API_TRACE("grpc_call_unref(c=%p)", 1, (this));

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
    GRPC_CALL_LOG_OP(GPR_INFO, elem, batch);
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_call_error_trace)) {
    gpr_log(GPR_INFO, "CancelWithError %s %s", is_client() ? "CLI" : "SVR",
            StatusToString(error).c_str());
  }
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
  if (GRPC_TRACE_FLAG_ENABLED(grpc_call_error_trace)) {
    gpr_log(GPR_INFO, "set_final_status %s %s", is_client() ? "CLI" : "SVR",
            StatusToString(error).c_str());
  }
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
                    gpr_log(GPR_DEBUG, "Append error: %s",
                            absl::StrCat("key=", StringViewFromSlice(md->key),
                                         " error=", error,
                                         " value=", value.as_string_view())
                                .c_str());
                  });
  }

  return true;
}

namespace {
class PublishToAppEncoder {
 public:
  explicit PublishToAppEncoder(grpc_metadata_array* dest,
                               const grpc_metadata_batch* encoding,
                               bool is_client)
      : dest_(dest), encoding_(encoding), is_client_(is_client) {}

  void Encode(const Slice& key, const Slice& value) {
    Append(key.c_slice(), value.c_slice());
  }

  // Catch anything that is not explicitly handled, and do not publish it to the
  // application. If new metadata is added to a batch that needs to be
  // published, it should be called out here.
  template <typename Which>
  void Encode(Which, const typename Which::ValueType&) {}

  void Encode(UserAgentMetadata, const Slice& slice) {
    Append(UserAgentMetadata::key(), slice);
  }

  void Encode(HostMetadata, const Slice& slice) {
    Append(HostMetadata::key(), slice);
  }

  void Encode(GrpcPreviousRpcAttemptsMetadata, uint32_t count) {
    Append(GrpcPreviousRpcAttemptsMetadata::key(), count);
  }

  void Encode(GrpcRetryPushbackMsMetadata, Duration count) {
    Append(GrpcRetryPushbackMsMetadata::key(), count.millis());
  }

  void Encode(LbTokenMetadata, const Slice& slice) {
    Append(LbTokenMetadata::key(), slice);
  }

 private:
  void Append(absl::string_view key, int64_t value) {
    Append(StaticSlice::FromStaticString(key).c_slice(),
           Slice::FromInt64(value).c_slice());
  }

  void Append(absl::string_view key, const Slice& value) {
    Append(StaticSlice::FromStaticString(key).c_slice(), value.c_slice());
  }

  void Append(grpc_slice key, grpc_slice value) {
    if (dest_->count == dest_->capacity) {
      Crash(absl::StrCat(
          "Too many metadata entries: capacity=", dest_->capacity, " on ",
          is_client_ ? "client" : "server", " encoding ", encoding_->count(),
          " elements: ", encoding_->DebugString().c_str()));
    }
    auto* mdusr = &dest_->metadata[dest_->count++];
    mdusr->key = key;
    mdusr->value = value;
  }

  grpc_metadata_array* const dest_;
  const grpc_metadata_batch* const encoding_;
  const bool is_client_;
};
}  // namespace

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
bool AreWriteFlagsValid(uint32_t flags) {
  // check that only bits in GRPC_WRITE_(INTERNAL?)_USED_MASK are set
  const uint32_t allowed_write_positions =
      (GRPC_WRITE_USED_MASK | GRPC_WRITE_INTERNAL_USED_MASK);
  const uint32_t invalid_positions = ~allowed_write_positions;
  return !(flags & invalid_positions);
}

bool AreInitialMetadataFlagsValid(uint32_t flags) {
  // check that only bits in GRPC_WRITE_(INTERNAL?)_USED_MASK are set
  uint32_t invalid_positions = ~GRPC_INITIAL_METADATA_USED_MASK;
  return !(flags & invalid_positions);
}

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
  bctl->call_tracer_ = static_cast<CallTracerAnnotationInterface*>(
      ContextGet(GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE));
  bctl->op_.payload = &stream_op_payload_;
  return bctl;
}

void FilterStackCall::BatchControl::PostCompletion() {
  FilterStackCall* call = call_;
  grpc_error_handle error = batch_error_.get();

  if (IsCallStatusOverrideOnCancellationEnabled()) {
    // On the client side, if final call status is already known (i.e if this op
    // includes recv_trailing_metadata) and if the call status is known to be
    // OK, then disregard the batch error to ensure call->receiving_buffer_ is
    // not cleared.
    if (op_.recv_trailing_metadata && call->is_client() &&
        call->status_error_.ok()) {
      error = absl::OkStatus();
    }
  }

  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG, "tag:%p batch_error=%s op:%s",
            completion_data_.notify_tag.tag, error.ToString().c_str(),
            grpc_transport_stream_op_batch_string(&op_, false).c_str());
  }

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
  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "tag:%p ReceivingStreamReady error=%s "
            "receiving_slice_buffer.has_value=%d recv_state=%" PRIdPTR,
            completion_data_.notify_tag.tag, error.ToString().c_str(),
            call_->receiving_slice_buffer_.has_value(),
            gpr_atm_no_barrier_load(&call_->recv_state_));
  }
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

namespace {
void EndOpImmediately(grpc_completion_queue* cq, void* notify_tag,
                      bool is_notify_tag_closure) {
  if (!is_notify_tag_closure) {
    CHECK(grpc_cq_begin_op(cq, notify_tag));
    grpc_cq_end_op(
        cq, notify_tag, absl::OkStatus(),
        [](void*, grpc_cq_completion* completion) { gpr_free(completion); },
        nullptr,
        static_cast<grpc_cq_completion*>(
            gpr_malloc(sizeof(grpc_cq_completion))));
  } else {
    Closure::Run(DEBUG_LOCATION, static_cast<grpc_closure*>(notify_tag),
                 absl::OkStatus());
  }
}
}  // namespace

grpc_call_error FilterStackCall::StartBatch(const grpc_op* ops, size_t nops,
                                            void* notify_tag,
                                            bool is_notify_tag_closure) {
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
    gpr_log(GPR_ERROR,
            "******************* SEND_STATUS WITH RECV_MESSAGE "
            "*******************");
    return GRPC_CALL_ERROR;
  }

  GRPC_CALL_LOG_BATCH(GPR_INFO, ops, nops);

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

  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG, "BATCH:%p START:%s BATCH:%s (tag:%p)", bctl,
            PendingOpString(pending_ops).c_str(),
            grpc_transport_stream_op_batch_string(stream_op, false).c_str(),
            bctl->completion_data_.notify_tag.tag);
  }
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

void FilterStackCall::ContextSet(grpc_context_index elem, void* value,
                                 void (*destroy)(void*)) {
  if (context_[elem].destroy) {
    context_[elem].destroy(context_[elem].value);
  }
  context_[elem].value = value;
  context_[elem].destroy = destroy;
}

///////////////////////////////////////////////////////////////////////////////
// Metadata validation helpers

namespace {
bool ValidateMetadata(size_t count, grpc_metadata* metadata) {
  if (count > INT_MAX) {
    return false;
  }
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
    }
  }
  return true;
}
}  // namespace

///////////////////////////////////////////////////////////////////////////////
// Utilities

namespace {
void PublishMetadataArray(grpc_metadata_batch* md, grpc_metadata_array* array,
                          bool is_client) {
  const auto md_count = md->count();
  if (md_count > array->capacity) {
    array->capacity =
        std::max(array->capacity + md->count(), array->capacity * 3 / 2);
    array->metadata = static_cast<grpc_metadata*>(
        gpr_realloc(array->metadata, sizeof(grpc_metadata) * array->capacity));
  }
  PublishToAppEncoder encoder(array, md, is_client);
  md->Encode(&encoder);
}

void CToMetadata(grpc_metadata* metadata, size_t count,
                 grpc_metadata_batch* b) {
  for (size_t i = 0; i < count; i++) {
    grpc_metadata* md = &metadata[i];
    auto key = StringViewFromSlice(md->key);
    // Filter "content-length metadata"
    if (key == "content-length") continue;
    b->Append(key, Slice(CSliceRef(md->value)),
              [md](absl::string_view error, const Slice& value) {
                gpr_log(GPR_DEBUG, "Append error: %s",
                        absl::StrCat("key=", StringViewFromSlice(md->key),
                                     " error=", error,
                                     " value=", value.as_string_view())
                            .c_str());
              });
  }
}

template <typename SetupResult, grpc_op_type kOp>
class OpHandlerImpl {
 public:
  using PromiseFactory = promise_detail::OncePromiseFactory<void, SetupResult>;
  using Promise = typename PromiseFactory::Promise;
  static_assert(!std::is_same<Promise, void>::value,
                "PromiseFactory must return a promise");

  OpHandlerImpl() : state_(State::kDismissed) {}
  explicit OpHandlerImpl(SetupResult result) : state_(State::kPromiseFactory) {
    Construct(&promise_factory_, std::move(result));
  }

  ~OpHandlerImpl() {
    switch (state_) {
      case State::kDismissed:
        break;
      case State::kPromiseFactory:
        Destruct(&promise_factory_);
        break;
      case State::kPromise:
        Destruct(&promise_);
        break;
    }
  }

  OpHandlerImpl(const OpHandlerImpl&) = delete;
  OpHandlerImpl& operator=(const OpHandlerImpl&) = delete;
  OpHandlerImpl(OpHandlerImpl&& other) noexcept : state_(other.state_) {
    switch (state_) {
      case State::kDismissed:
        break;
      case State::kPromiseFactory:
        Construct(&promise_factory_, std::move(other.promise_factory_));
        break;
      case State::kPromise:
        Construct(&promise_, std::move(other.promise_));
        break;
    }
  }
  OpHandlerImpl& operator=(OpHandlerImpl&& other) noexcept = delete;

  Poll<StatusFlag> operator()() {
    switch (state_) {
      case State::kDismissed:
        return Success{};
      case State::kPromiseFactory: {
        auto promise = promise_factory_.Make();
        Destruct(&promise_factory_);
        Construct(&promise_, std::move(promise));
        state_ = State::kPromise;
      }
        ABSL_FALLTHROUGH_INTENDED;
      case State::kPromise: {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO, "%sBeginPoll %s",
                  Activity::current()->DebugTag().c_str(), OpName());
        }
        auto r = poll_cast<StatusFlag>(promise_());
        if (grpc_call_trace.enabled()) {
          gpr_log(
              GPR_INFO, "%sEndPoll %s --> %s",
              Activity::current()->DebugTag().c_str(), OpName(),
              r.pending() ? "PENDING" : (r.value().ok() ? "OK" : "FAILURE"));
        }
        return r;
      }
    }
    GPR_UNREACHABLE_CODE(return Pending{});
  }

 private:
  enum class State {
    kDismissed,
    kPromiseFactory,
    kPromise,
  };

  static const char* OpName() {
    switch (kOp) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        return "SendInitialMetadata";
      case GRPC_OP_SEND_MESSAGE:
        return "SendMessage";
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        return "SendStatusFromServer";
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
        return "SendCloseFromClient";
      case GRPC_OP_RECV_MESSAGE:
        return "RecvMessage";
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        return "RecvCloseOnServer";
      case GRPC_OP_RECV_INITIAL_METADATA:
        return "RecvInitialMetadata";
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        return "RecvStatusOnClient";
    }
    Crash("Unreachable");
  }

  // gcc-12 has problems with this being a variant
  GPR_NO_UNIQUE_ADDRESS State state_;
  union {
    PromiseFactory promise_factory_;
    Promise promise_;
  };
};

class BatchOpIndex {
 public:
  BatchOpIndex(const grpc_op* ops, size_t nops) : ops_(ops) {
    for (size_t i = 0; i < nops; i++) {
      idxs_[ops[i].op] = static_cast<uint8_t>(i);
    }
  }

  // 1. Check if op_type is in the batch
  // 2. If it is, run the setup function in the context of the API call (NOT in
  // the call party).
  // 3. This setup function returns a promise factory which we'll then run *in*
  // the party to do initial setup,
  //    and have it return the promise that we'll ultimately poll on til
  //    completion.
  // Once we express our surface API in terms of core internal types this whole
  // dance will go away.
  template <grpc_op_type op_type, typename SetupFn>
  auto OpHandler(SetupFn setup) {
    using SetupResult = decltype(std::declval<SetupFn>()(grpc_op()));
    using Impl = OpHandlerImpl<SetupResult, op_type>;
    if (const grpc_op* op = this->op(op_type)) {
      auto r = setup(*op);
      return Impl(std::move(r));
    } else {
      return Impl();
    }
  }

  const grpc_op* op(grpc_op_type op_type) const {
    return idxs_[op_type] == 255 ? nullptr : &ops_[idxs_[op_type]];
  }

 private:
  const grpc_op* const ops_;
  std::array<uint8_t, 8> idxs_{255, 255, 255, 255, 255, 255, 255, 255};
};

template <typename FalliblePart, typename FinalPart>
auto InfallibleBatch(FalliblePart fallible_part, FinalPart final_part,
                     bool is_notify_tag_closure, void* notify_tag,
                     grpc_completion_queue* cq) {
  // Perform fallible_part, then final_part, then wait for the
  // completion queue to be done.
  // If cancelled, we'll ensure the completion queue is notified.
  // There's a slight bug here in that if we cancel this promise after
  // the WaitForCqEndOp we'll double post -- but we don't currently do that.
  return OnCancelFactory(
      [fallible_part = std::move(fallible_part),
       final_part = std::move(final_part), is_notify_tag_closure, notify_tag,
       cq]() mutable {
        return LogPollBatch(notify_tag,
                            Seq(std::move(fallible_part), std::move(final_part),
                                [is_notify_tag_closure, notify_tag, cq]() {
                                  return WaitForCqEndOp(is_notify_tag_closure,
                                                        notify_tag,
                                                        absl::OkStatus(), cq);
                                }));
      },
      [cq]() {
        grpc_cq_end_op(
            cq, nullptr, absl::OkStatus(),
            [](void*, grpc_cq_completion* completion) { delete completion; },
            nullptr, new grpc_cq_completion);
      });
}

template <typename FalliblePart>
auto FallibleBatch(FalliblePart fallible_part, bool is_notify_tag_closure,
                   void* notify_tag, grpc_completion_queue* cq) {
  // Perform fallible_part, then wait for the completion queue to be done.
  // If cancelled, we'll ensure the completion queue is notified.
  // There's a slight bug here in that if we cancel this promise after
  // the WaitForCqEndOp we'll double post -- but we don't currently do that.
  return OnCancelFactory(
      [fallible_part = std::move(fallible_part), is_notify_tag_closure,
       notify_tag, cq]() mutable {
        return LogPollBatch(
            notify_tag,
            Seq(std::move(fallible_part),
                [is_notify_tag_closure, notify_tag, cq](StatusFlag r) {
                  return WaitForCqEndOp(is_notify_tag_closure, notify_tag,
                                        StatusCast<absl::Status>(r), cq);
                }));
      },
      [cq]() {
        grpc_cq_end_op(
            cq, nullptr, absl::CancelledError(),
            [](void*, grpc_cq_completion* completion) { delete completion; },
            nullptr, new grpc_cq_completion);
      });
}

template <grpc_op_type op_type, typename PromiseFactory>
auto OpHandler(PromiseFactory setup) {
  return OpHandlerImpl<PromiseFactory, op_type>(std::move(setup));
}

template <typename F>
class PollBatchLogger {
 public:
  PollBatchLogger(void* tag, F f) : tag_(tag), f_(std::move(f)) {}

  auto operator()() {
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_INFO, "Poll batch %p", tag_);
    }
    auto r = f_();
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_INFO, "Poll batch %p --> %s", tag_, ResultString(r).c_str());
    }
    return r;
  }

 private:
  template <typename T>
  static std::string ResultString(Poll<T> r) {
    if (r.pending()) return "PENDING";
    return ResultString(r.value());
  }
  static std::string ResultString(Empty) { return "DONE"; }

  void* tag_;
  F f_;
};

template <typename F>
PollBatchLogger<F> LogPollBatch(void* tag, F f) {
  return PollBatchLogger<F>(tag, std::move(f));
}

grpc_call_error ValidateClientBatch(const grpc_op* ops, size_t nops) {
  BitSet<8> got_ops;
  for (size_t op_idx = 0; op_idx < nops; op_idx++) {
    const grpc_op& op = ops[op_idx];
    switch (op.op) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        if (!AreInitialMetadataFlagsValid(op.flags)) {
          return GRPC_CALL_ERROR_INVALID_FLAGS;
        }
        if (!ValidateMetadata(op.data.send_initial_metadata.count,
                              op.data.send_initial_metadata.metadata)) {
          return GRPC_CALL_ERROR_INVALID_METADATA;
        }
        break;
      case GRPC_OP_SEND_MESSAGE:
        if (!AreWriteFlagsValid(op.flags)) {
          return GRPC_CALL_ERROR_INVALID_FLAGS;
        }
        break;
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
      case GRPC_OP_RECV_INITIAL_METADATA:
      case GRPC_OP_RECV_MESSAGE:
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        if (op.flags != 0) return GRPC_CALL_ERROR_INVALID_FLAGS;
        break;
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        return GRPC_CALL_ERROR_NOT_ON_CLIENT;
    }
    if (got_ops.is_set(op.op)) return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
    got_ops.set(op.op);
  }
  return GRPC_CALL_OK;
}

grpc_call_error ValidateServerBatch(const grpc_op* ops, size_t nops) {
  BitSet<8> got_ops;
  for (size_t op_idx = 0; op_idx < nops; op_idx++) {
    const grpc_op& op = ops[op_idx];
    switch (op.op) {
      case GRPC_OP_SEND_INITIAL_METADATA:
        if (!AreInitialMetadataFlagsValid(op.flags)) {
          return GRPC_CALL_ERROR_INVALID_FLAGS;
        }
        if (!ValidateMetadata(op.data.send_initial_metadata.count,
                              op.data.send_initial_metadata.metadata)) {
          return GRPC_CALL_ERROR_INVALID_METADATA;
        }
        break;
      case GRPC_OP_SEND_MESSAGE:
        if (!AreWriteFlagsValid(op.flags)) {
          return GRPC_CALL_ERROR_INVALID_FLAGS;
        }
        break;
      case GRPC_OP_SEND_STATUS_FROM_SERVER:
        if (op.flags != 0) return GRPC_CALL_ERROR_INVALID_FLAGS;
        if (!ValidateMetadata(
                op.data.send_status_from_server.trailing_metadata_count,
                op.data.send_status_from_server.trailing_metadata)) {
          return GRPC_CALL_ERROR_INVALID_METADATA;
        }
        break;
      case GRPC_OP_RECV_MESSAGE:
      case GRPC_OP_RECV_CLOSE_ON_SERVER:
        if (op.flags != 0) return GRPC_CALL_ERROR_INVALID_FLAGS;
        break;
      case GRPC_OP_RECV_INITIAL_METADATA:
      case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
      case GRPC_OP_RECV_STATUS_ON_CLIENT:
        return GRPC_CALL_ERROR_NOT_ON_SERVER;
    }
    if (got_ops.is_set(op.op)) return GRPC_CALL_ERROR_TOO_MANY_OPERATIONS;
    got_ops.set(op.op);
  }
  return GRPC_CALL_OK;
}

class MessageReceiver {
 public:
  grpc_compression_algorithm incoming_compression_algorithm() const {
    return incoming_compression_algorithm_;
  }

  void SetIncomingCompressionAlgorithm(
      grpc_compression_algorithm incoming_compression_algorithm) {
    incoming_compression_algorithm_ = incoming_compression_algorithm;
  }

  uint32_t last_message_flags() const { return test_only_last_message_flags_; }

  template <typename Puller>
  auto MakeBatchOp(const grpc_op& op, Puller* puller) {
    CHECK_EQ(recv_message_, nullptr);
    recv_message_ = op.data.recv_message.recv_message;
    return [this, puller]() mutable {
      return Map(puller->PullMessage(),
                 [this](ValueOrFailure<absl::optional<MessageHandle>> msg) {
                   return FinishRecvMessage(std::move(msg));
                 });
    };
  }

 private:
  StatusFlag FinishRecvMessage(
      ValueOrFailure<absl::optional<MessageHandle>> result) {
    if (!result.ok()) {
      if (grpc_call_trace.enabled()) {
        gpr_log(GPR_INFO,
                "%s[call] RecvMessage: outstanding_recv "
                "finishes: received end-of-stream with error",
                Activity::current()->DebugTag().c_str());
      }
      *recv_message_ = nullptr;
      recv_message_ = nullptr;
      return Failure{};
    }
    if (!result->has_value()) {
      if (grpc_call_trace.enabled()) {
        gpr_log(GPR_INFO,
                "%s[call] RecvMessage: outstanding_recv "
                "finishes: received end-of-stream",
                Activity::current()->DebugTag().c_str());
      }
      *recv_message_ = nullptr;
      recv_message_ = nullptr;
      return Success{};
    }
    MessageHandle& message = **result;
    test_only_last_message_flags_ = message->flags();
    if ((message->flags() & GRPC_WRITE_INTERNAL_COMPRESS) &&
        (incoming_compression_algorithm_ != GRPC_COMPRESS_NONE)) {
      *recv_message_ = grpc_raw_compressed_byte_buffer_create(
          nullptr, 0, incoming_compression_algorithm_);
    } else {
      *recv_message_ = grpc_raw_byte_buffer_create(nullptr, 0);
    }
    grpc_slice_buffer_move_into(message->payload()->c_slice_buffer(),
                                &(*recv_message_)->data.raw.slice_buffer);
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_INFO,
              "%s[call] RecvMessage: outstanding_recv "
              "finishes: received %" PRIdPTR " byte message",
              Activity::current()->DebugTag().c_str(),
              (*recv_message_)->data.raw.slice_buffer.length);
    }
    recv_message_ = nullptr;
    return Success{};
  }

  grpc_byte_buffer** recv_message_ = nullptr;
  uint32_t test_only_last_message_flags_ = 0;
  // Compression algorithm for incoming data
  grpc_compression_algorithm incoming_compression_algorithm_ =
      GRPC_COMPRESS_NONE;
};

std::string MakeErrorString(const ServerMetadata* trailing_metadata) {
  std::string out = absl::StrCat(
      trailing_metadata->get(GrpcStatusFromWire()).value_or(false)
          ? "Error received from peer"
          : "Error generated by client",
      "grpc_status: ",
      grpc_status_code_to_string(trailing_metadata->get(GrpcStatusMetadata())
                                     .value_or(GRPC_STATUS_UNKNOWN)));
  if (const Slice* message =
          trailing_metadata->get_pointer(GrpcMessageMetadata())) {
    absl::StrAppend(&out, "\ngrpc_message: ", message->as_string_view());
  }
  if (auto annotations = trailing_metadata->get_pointer(GrpcStatusContext())) {
    absl::StrAppend(&out, "\nStatus Context:");
    for (const std::string& annotation : *annotations) {
      absl::StrAppend(&out, "\n  ", annotation);
    }
  }
  return out;
}
}  // namespace

///////////////////////////////////////////////////////////////////////////////
// CallSpine based Client Call

class ClientCall final
    : public Call,
      public DualRefCounted<ClientCall, NonPolymorphicRefCount, UnrefCallDtor> {
 public:
  ClientCall(grpc_call* parent_call, uint32_t propagation_mask,
             grpc_completion_queue* cq, Slice path,
             absl::optional<Slice> authority, bool registered_method,
             Timestamp deadline, grpc_compression_options compression_options,
             grpc_event_engine::experimental::EventEngine* event_engine,
             RefCountedPtr<Arena> arena,
             RefCountedPtr<UnstartedCallDestination> destination)
      : Call(false, deadline, std::move(arena), event_engine),
        cq_(cq),
        call_destination_(std::move(destination)),
        compression_options_(compression_options) {
    global_stats().IncrementClientCallsCreated();
    Construct(&send_initial_metadata_, Arena::MakePooled<ClientMetadata>());
    send_initial_metadata_->Set(HttpPathMetadata(), std::move(path));
    if (authority.has_value()) {
      send_initial_metadata_->Set(HttpAuthorityMetadata(),
                                  std::move(*authority));
    }
    send_initial_metadata_->Set(
        GrpcRegisteredMethod(),
        reinterpret_cast<void*>(static_cast<uintptr_t>(registered_method)));
  }

  ~ClientCall() override {
    switch (call_state_.load(std::memory_order_acquire)) {
      case kUnstarted:
      default:
        Destruct(&send_initial_metadata_);
        break;
      case kStarted:
        Destruct(&started_call_initiator_);
        break;
      case kCancelled:
        break;
    }
  }

  void CancelWithError(grpc_error_handle error) override;
  bool is_trailers_only() const override { return is_trailers_only_; }
  absl::string_view GetServerAuthority() const override {
    Crash("unimplemented");
  }
  grpc_call_error StartBatch(const grpc_op* ops, size_t nops, void* notify_tag,
                             bool is_notify_tag_closure) override;

  void ExternalRef() override { Ref().release(); }
  void ExternalUnref() override { Unref(); }
  void InternalRef(const char*) override { WeakRef().release(); }
  void InternalUnref(const char*) override { WeakUnref(); }

  void Orphaned() override {
    // TODO(ctiller): only when we're not already finished
    CancelWithError(absl::CancelledError());
  }

  void ContextSet(grpc_context_index elem, void* value,
                  void (*destroy)(void*)) override {
    legacy_context_[elem] = grpc_call_context_element{value, destroy};
  }

  void* ContextGet(grpc_context_index elem) const override {
    return legacy_context_[elem].value;
  }

  void SetCompletionQueue(grpc_completion_queue*) override {
    Crash("unimplemented");
  }

  grpc_compression_options compression_options() override {
    return compression_options_;
  }

  grpc_call_stack* call_stack() override { return nullptr; }

  char* GetPeer() override {
    Slice peer_slice = GetPeerString();
    if (!peer_slice.empty()) {
      absl::string_view peer_string_view = peer_slice.as_string_view();
      char* peer_string =
          static_cast<char*>(gpr_malloc(peer_string_view.size() + 1));
      memcpy(peer_string, peer_string_view.data(), peer_string_view.size());
      peer_string[peer_string_view.size()] = '\0';
      return peer_string;
    }
    return gpr_strdup("unknown");
  }

  bool Completed() final { Crash("unimplemented"); }
  bool failed_before_recv_message() const final { Crash("unimplemented"); }

  grpc_compression_algorithm incoming_compression_algorithm() override {
    return message_receiver_.incoming_compression_algorithm();
  }

  void SetIncomingCompressionAlgorithm(
      grpc_compression_algorithm algorithm) override {
    message_receiver_.SetIncomingCompressionAlgorithm(algorithm);
  }

  uint32_t test_only_message_flags() override {
    return message_receiver_.last_message_flags();
  }

 private:
  struct UnorderedStart {
    absl::AnyInvocable<void()> start_pending_batch;
    UnorderedStart* next;
  };

  void CommitBatch(const grpc_op* ops, size_t nops, void* notify_tag,
                   bool is_notify_tag_closure);
  template <typename Batch>
  void ScheduleCommittedBatch(Batch batch);
  void StartCall(const grpc_op& send_initial_metadata_op);
  StatusFlag FinishRecvMessage(
      ValueOrFailure<absl::optional<MessageHandle>> result);

  std::string DebugTag() { return absl::StrFormat("SERVER_CALL[%p]: ", this); }

  // call_state_ is one of:
  // 1. kUnstarted - call has not yet been started
  // 2. pointer to an UnorderedStart - call has ops started, but no send initial
  //    metadata yet
  // 3. kStarted - call has been started and call_initiator_ is ready
  // 4. kCancelled - call was cancelled before starting
  // In cases (1) and (2) send_initial_metadata_ is used to store the initial
  // but unsent metadata.
  // In case (3) started_call_initiator_ is used to store the call initiator.
  // In case (4) no other state is used.
  enum CallState : uintptr_t {
    kUnstarted = 0,
    kStarted = 1,
    kCancelled = 2,
  };
  std::atomic<uintptr_t> call_state_{kUnstarted};
  union {
    ClientMetadataHandle send_initial_metadata_;
    CallInitiator started_call_initiator_;
  };
  MessageReceiver message_receiver_;
  grpc_completion_queue* const cq_;
  const RefCountedPtr<UnstartedCallDestination> call_destination_;
  const grpc_compression_options compression_options_;
  ServerMetadataHandle received_initial_metadata_;
  ServerMetadataHandle received_trailing_metadata_;
  bool is_trailers_only_;
  grpc_call_context_element legacy_context_[GRPC_CONTEXT_COUNT] = {};
};

grpc_call_error ClientCall::StartBatch(const grpc_op* ops, size_t nops,
                                       void* notify_tag,
                                       bool is_notify_tag_closure) {
  if (nops == 0) {
    EndOpImmediately(cq_, notify_tag, is_notify_tag_closure);
    return GRPC_CALL_OK;
  }
  const grpc_call_error validation_result = ValidateClientBatch(ops, nops);
  if (validation_result != GRPC_CALL_OK) {
    return validation_result;
  }
  CommitBatch(ops, nops, notify_tag, is_notify_tag_closure);
  return GRPC_CALL_OK;
}

void ClientCall::CancelWithError(grpc_error_handle error) {
  auto cur_state = call_state_.load(std::memory_order_acquire);
  while (true) {
    switch (cur_state) {
      case kCancelled:
        return;
      case kUnstarted:
        if (call_state_.compare_exchange_strong(cur_state, kCancelled,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
          Destruct(&send_initial_metadata_);
          return;
        }
        break;
      case kStarted:
        started_call_initiator_.SpawnInfallible(
            "CancelWithError", [self = WeakRefAsSubclass<ClientCall>(),
                                error = std::move(error)]() mutable {
              self->started_call_initiator_.Cancel(std::move(error));
              return Empty{};
            });
        return;
      default:
        if (call_state_.compare_exchange_strong(cur_state, kCancelled,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
          auto* unordered_start = reinterpret_cast<UnorderedStart*>(cur_state);
          while (unordered_start != nullptr) {
            auto next = unordered_start->next;
            delete unordered_start;
            unordered_start = next;
          }
          return;
        }
    }
  }
}

template <typename Batch>
void ClientCall::ScheduleCommittedBatch(Batch batch) {
  auto cur_state = call_state_.load(std::memory_order_acquire);
  while (true) {
    switch (cur_state) {
      case kUnstarted:
      default: {  // UnorderedStart
        auto pending = std::make_unique<UnorderedStart>();
        pending->start_pending_batch = [this,
                                        batch = std::move(batch)]() mutable {
          started_call_initiator_.SpawnInfallible("batch", std::move(batch));
        };
        while (true) {
          pending->next = reinterpret_cast<UnorderedStart*>(cur_state);
          if (call_state_.compare_exchange_strong(
                  cur_state, reinterpret_cast<uintptr_t>(pending.get()),
                  std::memory_order_acq_rel, std::memory_order_acquire)) {
            std::ignore = pending.release();
            return;
          }
          if (cur_state == kStarted) {
            pending->start_pending_batch();
            return;
          }
          if (cur_state == kCancelled) {
            return;
          }
        }
      }
      case kStarted:
        started_call_initiator_.SpawnInfallible("batch", std::move(batch));
        return;
      case kCancelled:
        return;
    }
  }
}

void ClientCall::StartCall(const grpc_op& send_initial_metadata_op) {
  auto cur_state = call_state_.load(std::memory_order_acquire);
  PrepareOutgoingInitialMetadata(send_initial_metadata_op,
                                 *send_initial_metadata_);
  auto call = MakeCallPair(std::move(send_initial_metadata_), event_engine(),
                           arena()->Ref(), legacy_context_);
  Destruct(&send_initial_metadata_);
  Construct(&started_call_initiator_, std::move(call.initiator));
  call_destination_->StartCall(std::move(call.handler));
  while (true) {
    switch (cur_state) {
      case kUnstarted:
        if (call_state_.compare_exchange_strong(cur_state, kStarted,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
          return;
        }
        break;
      case kStarted:
        Crash("StartCall called twice");  // probably we crash earlier...
      case kCancelled:
        return;
      default: {  // UnorderedStart
        if (call_state_.compare_exchange_strong(cur_state, kStarted,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
          auto unordered_start = reinterpret_cast<UnorderedStart*>(cur_state);
          while (unordered_start->next != nullptr) {
            unordered_start->start_pending_batch();
            auto next = unordered_start->next;
            delete unordered_start;
            unordered_start = next;
          }
          return;
        }
        break;
      }
    }
  }
}

void ClientCall::CommitBatch(const grpc_op* ops, size_t nops, void* notify_tag,
                             bool is_notify_tag_closure) {
  if (nops == 1 && ops[0].op == GRPC_OP_SEND_INITIAL_METADATA) {
    StartCall(ops[0]);
    EndOpImmediately(cq_, notify_tag, is_notify_tag_closure);
    return;
  }
  if (!is_notify_tag_closure) grpc_cq_begin_op(cq_, notify_tag);
  BatchOpIndex op_index(ops, nops);
  auto send_message =
      op_index.OpHandler<GRPC_OP_SEND_MESSAGE>([this](const grpc_op& op) {
        SliceBuffer send;
        grpc_slice_buffer_swap(
            &op.data.send_message.send_message->data.raw.slice_buffer,
            send.c_slice_buffer());
        auto msg = arena()->MakePooled<Message>(std::move(send), op.flags);
        return [this, msg = std::move(msg)]() mutable {
          return started_call_initiator_.PushMessage(std::move(msg));
        };
      });
  auto send_close_from_client =
      op_index.OpHandler<GRPC_OP_SEND_CLOSE_FROM_CLIENT>(
          [this](const grpc_op& op) {
            return [this]() {
              started_call_initiator_.FinishSends();
              return Success{};
            };
          });
  auto recv_message =
      op_index.OpHandler<GRPC_OP_RECV_MESSAGE>([this](const grpc_op& op) {
        return message_receiver_.MakeBatchOp(op, &started_call_initiator_);
      });
  auto recv_initial_metadata =
      op_index.OpHandler<GRPC_OP_RECV_INITIAL_METADATA>([this](
                                                            const grpc_op& op) {
        return [this,
                array = op.data.recv_initial_metadata.recv_initial_metadata]() {
          return Map(
              started_call_initiator_.PullServerInitialMetadata(),
              [this,
               array](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
                ServerMetadataHandle metadata;
                if (!md.ok() || !md->has_value()) {
                  is_trailers_only_ = true;
                  metadata = Arena::MakePooled<ServerMetadata>();
                } else {
                  metadata = std::move(md->value());
                  is_trailers_only_ =
                      metadata->get(GrpcTrailersOnly()).value_or(false);
                }
                ProcessIncomingInitialMetadata(*metadata);
                PublishMetadataArray(metadata.get(), array, true);
                received_initial_metadata_ = std::move(metadata);
                return Success{};
              });
        };
      });
  auto primary_ops = AllOk<StatusFlag>(
      TrySeq(std::move(send_message), std::move(send_close_from_client)),
      TrySeq(std::move(recv_initial_metadata), std::move(recv_message)));
  if (const grpc_op* op = op_index.op(GRPC_OP_SEND_INITIAL_METADATA)) {
    StartCall(*op);
  }
  if (const grpc_op* op = op_index.op(GRPC_OP_RECV_STATUS_ON_CLIENT)) {
    ScheduleCommittedBatch(InfallibleBatch(
        std::move(primary_ops),
        OpHandler<GRPC_OP_RECV_STATUS_ON_CLIENT>(
            [this, out_status = op->data.recv_status_on_client.status,
             out_status_details = op->data.recv_status_on_client.status_details,
             out_error_string = op->data.recv_status_on_client.error_string,
             out_trailing_metadata =
                 op->data.recv_status_on_client.trailing_metadata]() {
              return Map(
                  started_call_initiator_.PullServerTrailingMetadata(),
                  [this, out_status, out_status_details, out_error_string,
                   out_trailing_metadata](
                      ServerMetadataHandle server_trailing_metadata) {
                    const auto status =
                        server_trailing_metadata->get(GrpcStatusMetadata())
                            .value_or(GRPC_STATUS_UNKNOWN);
                    *out_status = status;
                    Slice message_slice;
                    if (Slice* message = server_trailing_metadata->get_pointer(
                            GrpcMessageMetadata())) {
                      message_slice = message->Ref();
                    }
                    *out_status_details = message_slice.TakeCSlice();
                    if (out_error_string != nullptr) {
                      if (status != GRPC_STATUS_OK) {
                        *out_error_string = gpr_strdup(
                            MakeErrorString(server_trailing_metadata.get())
                                .c_str());
                      } else {
                        *out_error_string = nullptr;
                      }
                    }
                    PublishMetadataArray(server_trailing_metadata.get(),
                                         out_trailing_metadata, true);
                    received_trailing_metadata_ =
                        std::move(server_trailing_metadata);
                    return Success{};
                  });
            }),
        is_notify_tag_closure, notify_tag, cq_));
  } else {
    ScheduleCommittedBatch(FallibleBatch(
        std::move(primary_ops), is_notify_tag_closure, notify_tag, cq_));
  }
}

grpc_call* MakeClientCall(
    grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* cq, Slice path, absl::optional<Slice> authority,
    bool registered_method, Timestamp deadline,
    grpc_compression_options compression_options,
    grpc_event_engine::experimental::EventEngine* event_engine,
    RefCountedPtr<Arena> arena,
    RefCountedPtr<UnstartedCallDestination> destination) {
  return arena
      ->New<ClientCall>(parent_call, propagation_mask, cq, std::move(path),
                        std::move(authority), registered_method, deadline,
                        compression_options, event_engine, arena, destination)
      ->c_ptr();
}

///////////////////////////////////////////////////////////////////////////////
// CallSpine based Server Call

class ServerCall final : public Call, public DualRefCounted<ServerCall> {
 public:
  ServerCall(ClientMetadataHandle client_initial_metadata,
             CallHandler call_handler, ServerInterface* server,
             grpc_completion_queue* cq)
      : Call(false,
             client_initial_metadata->get(GrpcTimeoutMetadata())
                 .value_or(Timestamp::InfFuture()),
             call_handler.arena()->Ref(), call_handler.event_engine()),
        call_handler_(std::move(call_handler)),
        client_initial_metadata_stored_(std::move(client_initial_metadata)),
        cq_(cq),
        server_(server) {
    call_handler_.legacy_context()[GRPC_CONTEXT_CALL].value =
        static_cast<Call*>(this);
    global_stats().IncrementServerCallsCreated();
  }

  void CancelWithError(grpc_error_handle error) override {
    call_handler_.SpawnInfallible(
        "CancelWithError",
        [self = WeakRefAsSubclass<ServerCall>(), error = std::move(error)] {
          auto status = ServerMetadataFromStatus(error);
          status->Set(GrpcCallWasCancelled(), true);
          self->call_handler_.PushServerTrailingMetadata(std::move(status));
          return Empty{};
        });
  }
  bool is_trailers_only() const override {
    Crash("is_trailers_only not implemented for server calls");
  }
  absl::string_view GetServerAuthority() const override {
    Crash("unimplemented");
  }
  grpc_call_error StartBatch(const grpc_op* ops, size_t nops, void* notify_tag,
                             bool is_notify_tag_closure) override;

  void ExternalRef() override { Ref().release(); }
  void ExternalUnref() override { Unref(); }
  void InternalRef(const char*) override { WeakRef().release(); }
  void InternalUnref(const char*) override { WeakUnref(); }

  void Orphaned() override {
    // TODO(ctiller): only when we're not already finished
    CancelWithError(absl::CancelledError());
  }

  void ContextSet(grpc_context_index elem, void* value,
                  void (*destroy)(void*)) override {
    call_handler_.legacy_context()[elem] =
        grpc_call_context_element{value, destroy};
  }

  void* ContextGet(grpc_context_index elem) const override {
    return call_handler_.legacy_context()[elem].value;
  }

  void SetCompletionQueue(grpc_completion_queue*) override {
    Crash("unimplemented");
  }

  grpc_compression_options compression_options() override {
    return server_->compression_options();
  }

  grpc_call_stack* call_stack() override { return nullptr; }

  char* GetPeer() override {
    Slice peer_slice = GetPeerString();
    if (!peer_slice.empty()) {
      absl::string_view peer_string_view = peer_slice.as_string_view();
      char* peer_string =
          static_cast<char*>(gpr_malloc(peer_string_view.size() + 1));
      memcpy(peer_string, peer_string_view.data(), peer_string_view.size());
      peer_string[peer_string_view.size()] = '\0';
      return peer_string;
    }
    return gpr_strdup("unknown");
  }

  bool Completed() final { Crash("unimplemented"); }
  bool failed_before_recv_message() const final { Crash("unimplemented"); }

  uint32_t test_only_message_flags() override {
    return message_receiver_.last_message_flags();
  }

  grpc_compression_algorithm incoming_compression_algorithm() override {
    return message_receiver_.incoming_compression_algorithm();
  }

  void SetIncomingCompressionAlgorithm(
      grpc_compression_algorithm algorithm) override {
    message_receiver_.SetIncomingCompressionAlgorithm(algorithm);
  }

 private:
  void CommitBatch(const grpc_op* ops, size_t nops, void* notify_tag,
                   bool is_notify_tag_closure);

  std::string DebugTag() { return absl::StrFormat("SERVER_CALL[%p]: ", this); }

  CallHandler call_handler_;
  MessageReceiver message_receiver_;
  ClientMetadataHandle client_initial_metadata_stored_;
  grpc_completion_queue* const cq_;
  ServerInterface* const server_;
};

grpc_call_error ServerCall::StartBatch(const grpc_op* ops, size_t nops,
                                       void* notify_tag,
                                       bool is_notify_tag_closure) {
  if (nops == 0) {
    EndOpImmediately(cq_, notify_tag, is_notify_tag_closure);
    return GRPC_CALL_OK;
  }
  const grpc_call_error validation_result = ValidateServerBatch(ops, nops);
  if (validation_result != GRPC_CALL_OK) {
    return validation_result;
  }
  CommitBatch(ops, nops, notify_tag, is_notify_tag_closure);
  return GRPC_CALL_OK;
}

void ServerCall::CommitBatch(const grpc_op* ops, size_t nops, void* notify_tag,
                             bool is_notify_tag_closure) {
  BatchOpIndex op_index(ops, nops);
  if (!is_notify_tag_closure) grpc_cq_begin_op(cq_, notify_tag);
  auto send_initial_metadata =
      op_index.OpHandler<GRPC_OP_SEND_INITIAL_METADATA>([this](
                                                            const grpc_op& op) {
        auto metadata = arena()->MakePooled<ServerMetadata>();
        PrepareOutgoingInitialMetadata(op, *metadata);
        CToMetadata(op.data.send_initial_metadata.metadata,
                    op.data.send_initial_metadata.count, metadata.get());
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO, "%s[call] Send initial metadata",
                  DebugTag().c_str());
        }
        return [this, metadata = std::move(metadata)]() mutable {
          return call_handler_.PushServerInitialMetadata(std::move(metadata));
        };
      });
  auto send_message =
      op_index.OpHandler<GRPC_OP_SEND_MESSAGE>([this](const grpc_op& op) {
        SliceBuffer send;
        grpc_slice_buffer_swap(
            &op.data.send_message.send_message->data.raw.slice_buffer,
            send.c_slice_buffer());
        auto msg = arena()->MakePooled<Message>(std::move(send), op.flags);
        return [this, msg = std::move(msg)]() mutable {
          return call_handler_.PushMessage(std::move(msg));
        };
      });
  auto send_trailing_metadata =
      op_index.OpHandler<GRPC_OP_SEND_STATUS_FROM_SERVER>(
          [this](const grpc_op& op) {
            auto metadata = arena()->MakePooled<ServerMetadata>();
            CToMetadata(op.data.send_status_from_server.trailing_metadata,
                        op.data.send_status_from_server.trailing_metadata_count,
                        metadata.get());
            metadata->Set(GrpcStatusMetadata(),
                          op.data.send_status_from_server.status);
            if (auto* details =
                    op.data.send_status_from_server.status_details) {
              // TODO(ctiller): this should not be a copy, but we have
              // callers that allocate and pass in a slice created with
              // grpc_slice_from_static_string and then delete the string
              // after passing it in, which shouldn't be a supported API.
              metadata->Set(GrpcMessageMetadata(),
                            Slice(grpc_slice_copy(*details)));
            }
            CHECK(metadata != nullptr);
            return [this, metadata = std::move(metadata)]() mutable {
              CHECK(metadata != nullptr);
              return [this, metadata = std::move(
                                metadata)]() mutable -> Poll<Success> {
                CHECK(metadata != nullptr);
                call_handler_.PushServerTrailingMetadata(std::move(metadata));
                return Success{};
              };
            };
          });
  auto recv_message =
      op_index.OpHandler<GRPC_OP_RECV_MESSAGE>([this](const grpc_op& op) {
        return message_receiver_.MakeBatchOp(op, &call_handler_);
      });
  auto primary_ops = AllOk<StatusFlag>(
      TrySeq(AllOk<StatusFlag>(std::move(send_initial_metadata),
                               std::move(send_message)),
             std::move(send_trailing_metadata)),
      std::move(recv_message));
  if (auto* op = op_index.op(GRPC_OP_RECV_CLOSE_ON_SERVER)) {
    auto recv_trailing_metadata = OpHandler<GRPC_OP_RECV_CLOSE_ON_SERVER>(
        [this, cancelled = op->data.recv_close_on_server.cancelled]() {
          return Map(call_handler_.WasCancelled(),
                     [cancelled, this](bool result) -> Success {
                       ResetDeadline();
                       *cancelled = result ? 1 : 0;
                       return Success{};
                     });
        });
    call_handler_.SpawnInfallible(
        "final-batch", InfallibleBatch(std::move(primary_ops),
                                       std::move(recv_trailing_metadata),
                                       is_notify_tag_closure, notify_tag, cq_));
  } else {
    call_handler_.SpawnInfallible(
        "batch", FallibleBatch(std::move(primary_ops), is_notify_tag_closure,
                               notify_tag, cq_));
  }
}

grpc_call* MakeServerCall(CallHandler call_handler,
                          ClientMetadataHandle client_initial_metadata,
                          ServerInterface* server, grpc_completion_queue* cq,
                          grpc_metadata_array* publish_initial_metadata) {
  PublishMetadataArray(client_initial_metadata.get(), publish_initial_metadata,
                       false);
  // TODO(ctiller): ideally we'd put this in the arena with the CallHandler,
  // but there's an ownership problem: CallHandler owns the arena, and so would
  // get destroyed before the base class Call destructor runs, leading to
  // UB/crash. Investigate another path.
  return (new ServerCall(std::move(client_initial_metadata),
                         std::move(call_handler), server, cq))
      ->c_ptr();
}

}  // namespace grpc_core

///////////////////////////////////////////////////////////////////////////////
// C-based API

void* grpc_call_arena_alloc(grpc_call* call, size_t size) {
  grpc_core::ExecCtx exec_ctx;
  return grpc_core::Call::FromC(call)->arena()->Alloc(size);
}

size_t grpc_call_get_initial_size_estimate() {
  return grpc_core::FilterStackCall::InitialSizeEstimate();
}

grpc_error_handle grpc_call_create(grpc_call_create_args* args,
                                   grpc_call** out_call) {
  return grpc_core::FilterStackCall::Create(args, out_call);
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

grpc_call* grpc_call_from_top_element(grpc_call_element* surface_element) {
  return grpc_core::FilterStackCall::FromTopElem(surface_element)->c_ptr();
}

grpc_call_error grpc_call_cancel(grpc_call* call, void* reserved) {
  GRPC_API_TRACE("grpc_call_cancel(call=%p, reserved=%p)", 2, (call, reserved));
  CHECK_EQ(reserved, nullptr);
  if (call == nullptr) {
    return GRPC_CALL_ERROR;
  }
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Call::FromC(call)->CancelWithError(absl::CancelledError());
  return GRPC_CALL_OK;
}

grpc_call_error grpc_call_cancel_with_status(grpc_call* c,
                                             grpc_status_code status,
                                             const char* description,
                                             void* reserved) {
  GRPC_API_TRACE(
      "grpc_call_cancel_with_status("
      "c=%p, status=%d, description=%s, reserved=%p)",
      4, (c, (int)status, description, reserved));
  CHECK_EQ(reserved, nullptr);
  if (c == nullptr) {
    return GRPC_CALL_ERROR;
  }
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Call::FromC(c)->CancelWithStatus(status, description);
  return GRPC_CALL_OK;
}

void grpc_call_cancel_internal(grpc_call* call) {
  grpc_core::Call::FromC(call)->CancelWithError(absl::CancelledError());
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
  GRPC_API_TRACE(
      "grpc_call_start_batch(call=%p, ops=%p, nops=%lu, tag=%p, "
      "reserved=%p)",
      5, (call, ops, (unsigned long)nops, tag, reserved));

  if (reserved != nullptr || call == nullptr) {
    return GRPC_CALL_ERROR;
  } else {
    grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
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

void grpc_call_context_set(grpc_call* call, grpc_context_index elem,
                           void* value, void (*destroy)(void* value)) {
  return grpc_core::Call::FromC(call)->ContextSet(elem, value, destroy);
}

void* grpc_call_context_get(grpc_call* call, grpc_context_index elem) {
  return grpc_core::Call::FromC(call)->ContextGet(elem);
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
  grpc_core::Call::FromC(call)->event_engine()->Run(std::move(cb));
}
