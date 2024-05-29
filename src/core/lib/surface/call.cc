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
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/compression/compression_internal.h"
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
#include "src/core/telemetry/call_tracer.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
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

Call::Call(bool is_client, Timestamp send_deadline, RefCountedPtr<Arena> arena,
           grpc_event_engine::experimental::EventEngine* event_engine)
    : arena_(std::move(arena)),
      send_deadline_(send_deadline),
      is_client_(is_client),
      event_engine_(event_engine) {
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
                           arena()->Ref());
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
