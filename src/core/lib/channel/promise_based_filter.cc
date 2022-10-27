// Copyright 2022 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/promise_based_filter.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/variant.h"

#include <grpc/status.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice.h"

extern grpc_core::TraceFlag grpc_trace_channel;

namespace grpc_core {
namespace promise_filter_detail {

namespace {
class FakeActivity final : public Activity {
 public:
  void Orphan() override {}
  void ForceImmediateRepoll() override {}
  Waker MakeOwningWaker() override { abort(); }
  Waker MakeNonOwningWaker() override { abort(); }
  void Run(absl::FunctionRef<void()> f) {
    ScopedActivity activity(this);
    f();
  }
};

absl::Status StatusFromMetadata(const ServerMetadata& md) {
  auto status_code = md.get(GrpcStatusMetadata()).value_or(GRPC_STATUS_UNKNOWN);
  if (status_code == GRPC_STATUS_OK) {
    return absl::OkStatus();
  }
  const auto* message = md.get_pointer(GrpcMessageMetadata());
  return grpc_error_set_int(
      absl::Status(static_cast<absl::StatusCode>(status_code),
                   message == nullptr ? "" : message->as_string_view()),
      StatusIntProperty::kRpcStatus, status_code);
}
}  // namespace

///////////////////////////////////////////////////////////////////////////////
// BaseCallData

BaseCallData::BaseCallData(grpc_call_element* elem,
                           const grpc_call_element_args* args, uint8_t flags)
    : call_stack_(args->call_stack),
      elem_(elem),
      arena_(args->arena),
      call_combiner_(args->call_combiner),
      deadline_(args->deadline),
      context_(args->context),
      server_initial_metadata_latch_(
          flags & kFilterExaminesServerInitialMetadata
              ? arena_->New<Latch<ServerMetadata*>>()
              : nullptr),
      send_message_(flags & kFilterExaminesOutboundMessages
                        ? arena_->New<SendMessage>(this)
                        : nullptr),
      receive_message_(flags & kFilterExaminesInboundMessages
                           ? arena_->New<ReceiveMessage>(this)
                           : nullptr),
      event_engine_(
          static_cast<ChannelFilter*>(elem->channel_data)
              ->hack_until_per_channel_stack_event_engines_land_get_event_engine()) {
}

BaseCallData::~BaseCallData() {
  FakeActivity().Run([this] {
    if (send_message_ != nullptr) {
      send_message_->~SendMessage();
    }
    if (receive_message_ != nullptr) {
      receive_message_->~ReceiveMessage();
    }
    if (server_initial_metadata_latch_ != nullptr) {
      server_initial_metadata_latch_->~Latch();
    }
  });
}

// We don't form ActivityPtr's to this type, and consequently don't need
// Orphan().
void BaseCallData::Orphan() { abort(); }

// For now we don't care about owning/non-owning wakers, instead just share
// implementation.
Waker BaseCallData::MakeNonOwningWaker() { return MakeOwningWaker(); }

Waker BaseCallData::MakeOwningWaker() {
  GRPC_CALL_STACK_REF(call_stack_, "waker");
  return Waker(this);
}

void BaseCallData::Wakeup() {
  auto wakeup = [](void* p, grpc_error_handle) {
    auto* self = static_cast<BaseCallData*>(p);
    self->OnWakeup();
    self->Drop();
  };
  auto* closure = GRPC_CLOSURE_CREATE(wakeup, this, nullptr);
  GRPC_CALL_COMBINER_START(call_combiner_, closure, absl::OkStatus(), "wakeup");
}

void BaseCallData::Drop() { GRPC_CALL_STACK_UNREF(call_stack_, "waker"); }

std::string BaseCallData::LogTag() const {
  return absl::StrCat(
      ClientOrServerString(), "[", elem_->filter->name, ":0x",
      absl::Hex(reinterpret_cast<uintptr_t>(elem_), absl::kZeroPad8), "]");
}

///////////////////////////////////////////////////////////////////////////////
// BaseCallData::CapturedBatch

namespace {
uintptr_t* RefCountField(grpc_transport_stream_op_batch* b) {
  return &b->handler_private.closure.error_data.scratch;
}
}  // namespace

BaseCallData::CapturedBatch::CapturedBatch() : batch_(nullptr) {}

BaseCallData::CapturedBatch::CapturedBatch(
    grpc_transport_stream_op_batch* batch) {
  *RefCountField(batch) = 1;
  batch_ = batch;
}

BaseCallData::CapturedBatch::~CapturedBatch() {
  if (batch_ == nullptr) return;
  // A ref can be dropped by destruction, but it must not release the batch
  uintptr_t& refcnt = *RefCountField(batch_);
  if (refcnt == 0) return;  // refcnt==0 ==> cancelled
  --refcnt;
  GPR_ASSERT(refcnt != 0);
}

BaseCallData::CapturedBatch::CapturedBatch(const CapturedBatch& rhs)
    : batch_(rhs.batch_) {
  if (batch_ == nullptr) return;
  uintptr_t& refcnt = *RefCountField(batch_);
  if (refcnt == 0) return;  // refcnt==0 ==> cancelled
  ++refcnt;
}

BaseCallData::CapturedBatch& BaseCallData::CapturedBatch::operator=(
    const CapturedBatch& b) {
  CapturedBatch temp(b);
  Swap(&temp);
  return *this;
}

BaseCallData::CapturedBatch::CapturedBatch(CapturedBatch&& rhs) noexcept
    : batch_(rhs.batch_) {
  rhs.batch_ = nullptr;
}

BaseCallData::CapturedBatch& BaseCallData::CapturedBatch::operator=(
    CapturedBatch&& b) noexcept {
  Swap(&b);
  return *this;
}

void BaseCallData::CapturedBatch::ResumeWith(Flusher* releaser) {
  auto* batch = std::exchange(batch_, nullptr);
  GPR_ASSERT(batch != nullptr);
  uintptr_t& refcnt = *RefCountField(batch);
  if (refcnt == 0) return;  // refcnt==0 ==> cancelled
  if (--refcnt == 0) {
    releaser->Resume(batch);
  }
}

void BaseCallData::CapturedBatch::CompleteWith(Flusher* releaser) {
  auto* batch = std::exchange(batch_, nullptr);
  GPR_ASSERT(batch != nullptr);
  uintptr_t& refcnt = *RefCountField(batch);
  if (refcnt == 0) return;  // refcnt==0 ==> cancelled
  if (--refcnt == 0) {
    releaser->Complete(batch);
  }
}

void BaseCallData::CapturedBatch::CancelWith(grpc_error_handle error,
                                             Flusher* releaser) {
  auto* batch = std::exchange(batch_, nullptr);
  GPR_ASSERT(batch != nullptr);
  uintptr_t& refcnt = *RefCountField(batch);
  if (refcnt == 0) {
    // refcnt==0 ==> cancelled
    return;
  }
  refcnt = 0;
  releaser->Cancel(batch, error);
}

///////////////////////////////////////////////////////////////////////////////
// BaseCallData::Flusher

BaseCallData::Flusher::Flusher(BaseCallData* call) : call_(call) {
  GRPC_CALL_STACK_REF(call_->call_stack(), "flusher");
}

BaseCallData::Flusher::~Flusher() {
  if (release_.empty()) {
    if (call_closures_.size() == 0) {
      GRPC_CALL_COMBINER_STOP(call_->call_combiner(), "nothing to flush");
      GRPC_CALL_STACK_UNREF(call_->call_stack(), "flusher");
      return;
    }
    call_closures_.RunClosures(call_->call_combiner());
    GRPC_CALL_STACK_UNREF(call_->call_stack(), "flusher");
    return;
  }
  auto call_next_op = [](void* p, grpc_error_handle) {
    auto* batch = static_cast<grpc_transport_stream_op_batch*>(p);
    BaseCallData* call =
        static_cast<BaseCallData*>(batch->handler_private.extra_arg);
    if (grpc_trace_channel.enabled()) {
      gpr_log(GPR_DEBUG, "FLUSHER:forward batch via closure: %s",
              grpc_transport_stream_op_batch_string(batch).c_str());
    }
    grpc_call_next_op(call->elem(), batch);
    GRPC_CALL_STACK_UNREF(call->call_stack(), "flusher_batch");
  };
  for (size_t i = 1; i < release_.size(); i++) {
    auto* batch = release_[i];
    if (grpc_trace_channel.enabled()) {
      gpr_log(GPR_DEBUG, "FLUSHER:queue batch to forward in closure: %s",
              grpc_transport_stream_op_batch_string(release_[i]).c_str());
    }
    batch->handler_private.extra_arg = call_;
    GRPC_CLOSURE_INIT(&batch->handler_private.closure, call_next_op, batch,
                      nullptr);
    GRPC_CALL_STACK_REF(call_->call_stack(), "flusher_batch");
    call_closures_.Add(&batch->handler_private.closure, absl::OkStatus(),
                       "flusher_batch");
  }
  call_closures_.RunClosuresWithoutYielding(call_->call_combiner());
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "FLUSHER:forward batch: %s",
            grpc_transport_stream_op_batch_string(release_[0]).c_str());
  }
  grpc_call_next_op(call_->elem(), release_[0]);
  GRPC_CALL_STACK_UNREF(call_->call_stack(), "flusher");
}

///////////////////////////////////////////////////////////////////////////////
// BaseCallData::SendMessage

const char* BaseCallData::SendMessage::StateString(State state) {
  switch (state) {
    case State::kInitial:
      return "INITIAL";
    case State::kIdle:
      return "IDLE";
    case State::kGotBatchNoPipe:
      return "GOT_BATCH_NO_PIPE";
    case State::kGotBatch:
      return "GOT_BATCH";
    case State::kPushedToPipe:
      return "PUSHED_TO_PIPE";
    case State::kForwardedBatch:
      return "FORWARDED_BATCH";
    case State::kBatchCompleted:
      return "BATCH_COMPLETED";
    case State::kCancelled:
      return "CANCELLED";
  }
  return "UNKNOWN";
}

void BaseCallData::SendMessage::StartOp(CapturedBatch batch) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s SendMessage.StartOp st=%s", base_->LogTag().c_str(),
            StateString(state_));
  }
  switch (state_) {
    case State::kInitial:
      state_ = State::kGotBatchNoPipe;
      break;
    case State::kIdle:
      state_ = State::kGotBatch;
      break;
    case State::kGotBatch:
    case State::kGotBatchNoPipe:
    case State::kForwardedBatch:
    case State::kBatchCompleted:
    case State::kPushedToPipe:
      abort();
    case State::kCancelled:
      return;
  }
  batch_ = batch;
  intercepted_on_complete_ = std::exchange(batch_->on_complete, &on_complete_);
}

void BaseCallData::SendMessage::GotPipe(PipeReceiver<MessageHandle>* receiver) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s SendMessage.GotPipe st=%s", base_->LogTag().c_str(),
            StateString(state_));
  }
  GPR_ASSERT(receiver != nullptr);
  switch (state_) {
    case State::kInitial:
      state_ = State::kIdle;
      Activity::current()->ForceImmediateRepoll();
      break;
    case State::kGotBatchNoPipe:
      state_ = State::kGotBatch;
      Activity::current()->ForceImmediateRepoll();
      break;
    case State::kIdle:
    case State::kGotBatch:
    case State::kForwardedBatch:
    case State::kBatchCompleted:
    case State::kPushedToPipe:
      abort();
    case State::kCancelled:
      return;
  }
  receiver_ = receiver;
}

bool BaseCallData::SendMessage::IsIdle() const {
  switch (state_) {
    case State::kInitial:
    case State::kIdle:
    case State::kForwardedBatch:
    case State::kCancelled:
      return true;
    case State::kGotBatchNoPipe:
    case State::kGotBatch:
    case State::kBatchCompleted:
    case State::kPushedToPipe:
      return false;
  }
  GPR_UNREACHABLE_CODE(return false);
}

void BaseCallData::SendMessage::OnComplete(absl::Status status) {
  Flusher flusher(base_);
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s SendMessage.OnComplete st=%s status=%s",
            base_->LogTag().c_str(), StateString(state_),
            status.ToString().c_str());
  }
  switch (state_) {
    case State::kInitial:
    case State::kIdle:
    case State::kGotBatchNoPipe:
    case State::kPushedToPipe:
    case State::kGotBatch:
    case State::kBatchCompleted:
      abort();
      break;
    case State::kCancelled:
      flusher.AddClosure(intercepted_on_complete_, status,
                         "forward after cancel");
      break;
    case State::kForwardedBatch:
      completed_status_ = status;
      state_ = State::kBatchCompleted;
      base_->WakeInsideCombiner(&flusher);
      break;
  }
}

void BaseCallData::SendMessage::Done(const ServerMetadata& metadata) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s SendMessage.Done st=%s md=%s",
            base_->LogTag().c_str(), StateString(state_),
            metadata.DebugString().c_str());
  }
  switch (state_) {
    case State::kCancelled:
      break;
    case State::kInitial:
    case State::kIdle:
    case State::kForwardedBatch:
      state_ = State::kCancelled;
      break;
    case State::kGotBatchNoPipe:
    case State::kGotBatch:
    case State::kBatchCompleted:
      abort();
      break;
    case State::kPushedToPipe:
      push_.reset();
      next_.reset();
      state_ = State::kCancelled;
      break;
  }
}

void BaseCallData::SendMessage::WakeInsideCombiner(Flusher* flusher) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s SendMessage.WakeInsideCombiner st=%s%s",
            base_->LogTag().c_str(), StateString(state_),
            state_ == State::kBatchCompleted
                ? absl::StrCat(" status=", completed_status_.ToString()).c_str()
                : "");
  }
  switch (state_) {
    case State::kInitial:
    case State::kIdle:
    case State::kGotBatchNoPipe:
    case State::kForwardedBatch:
    case State::kCancelled:
      break;
    case State::kGotBatch: {
      state_ = State::kPushedToPipe;
      auto message = GetContext<Arena>()->MakePooled<Message>();
      message->payload()->Swap(batch_->payload->send_message.send_message);
      message->mutable_flags() = batch_->payload->send_message.flags;
      push_ = pipe_.sender.Push(std::move(message));
      next_ = receiver_->Next();
    }
      ABSL_FALLTHROUGH_INTENDED;
    case State::kPushedToPipe: {
      GPR_ASSERT(push_.has_value());
      auto r_push = (*push_)();
      if (auto* p = absl::get_if<bool>(&r_push)) {
        if (grpc_trace_channel.enabled()) {
          gpr_log(GPR_DEBUG,
                  "%s SendMessage.WakeInsideCombiner push complete, result=%s",
                  base_->LogTag().c_str(), *p ? "true" : "false");
        }
        // We haven't pulled through yet, so this certainly shouldn't succeed.
        GPR_ASSERT(!*p);
        state_ = State::kCancelled;
        batch_.CancelWith(absl::CancelledError(), flusher);
        break;
      }
      GPR_ASSERT(next_.has_value());
      auto r_next = (*next_)();
      if (auto* p = absl::get_if<NextResult<MessageHandle>>(&r_next)) {
        if (grpc_trace_channel.enabled()) {
          gpr_log(GPR_DEBUG,
                  "%s SendMessage.WakeInsideCombiner next complete, "
                  "result.has_value=%s",
                  base_->LogTag().c_str(), p->has_value() ? "true" : "false");
        }
        GPR_ASSERT(p->has_value());
        batch_->payload->send_message.send_message->Swap((**p)->payload());
        batch_->payload->send_message.flags = (**p)->flags();
        state_ = State::kForwardedBatch;
        batch_.ResumeWith(flusher);
        next_result_ = std::move(*p);
        next_.reset();
      }
    } break;
    case State::kBatchCompleted:
      next_result_.reset();
      // We've cleared out the NextResult on the pipe from promise to us, but
      // there's also the pipe from us to the promise (so that the promise can
      // intercept the sent messages). The push promise here is pushing into the
      // latter pipe, and so we need to keep polling it until it's done, which
      // depending on what happens inside the promise may take some time.
      if (absl::holds_alternative<Pending>((*push_)())) break;
      if (completed_status_.ok()) {
        state_ = State::kIdle;
        Activity::current()->ForceImmediateRepoll();
      } else {
        state_ = State::kCancelled;
      }
      push_.reset();
      flusher->AddClosure(intercepted_on_complete_, completed_status_,
                          "batch_completed");
      break;
  }
}

///////////////////////////////////////////////////////////////////////////////
// BaseCallData::ReceiveMessage

const char* BaseCallData::ReceiveMessage::StateString(State state) {
  switch (state) {
    case State::kInitial:
      return "INITIAL";
    case State::kIdle:
      return "IDLE";
    case State::kForwardedBatchNoPipe:
      return "FORWARDED_BATCH_NO_PIPE";
    case State::kForwardedBatch:
      return "FORWARDED_BATCH";
    case State::kBatchCompletedNoPipe:
      return "BATCH_COMPLETED_NO_PIPE";
    case State::kBatchCompleted:
      return "BATCH_COMPLETED";
    case State::kPushedToPipe:
      return "PUSHED_TO_PIPE";
    case State::kPulledFromPipe:
      return "PULLED_FROM_PIPE";
    case State::kCancelled:
      return "CANCELLED";
    case State::kCancelledWhilstForwarding:
      return "CANCELLED_WHILST_FORWARDING";
    case State::kBatchCompletedButCancelled:
      return "BATCH_COMPLETED_BUT_CANCELLED";
  }
  return "UNKNOWN";
}

void BaseCallData::ReceiveMessage::StartOp(CapturedBatch& batch) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s ReceiveMessage.StartOp st=%s",
            base_->LogTag().c_str(), StateString(state_));
  }
  switch (state_) {
    case State::kInitial:
      state_ = State::kForwardedBatchNoPipe;
      break;
    case State::kIdle:
      state_ = State::kForwardedBatch;
      break;
    case State::kCancelledWhilstForwarding:
    case State::kBatchCompletedButCancelled:
    case State::kForwardedBatch:
    case State::kForwardedBatchNoPipe:
    case State::kBatchCompleted:
    case State::kBatchCompletedNoPipe:
    case State::kPushedToPipe:
    case State::kPulledFromPipe:
      abort();
    case State::kCancelled:
      return;
  }
  intercepted_slice_buffer_ = batch->payload->recv_message.recv_message;
  intercepted_flags_ = batch->payload->recv_message.flags;
  if (intercepted_flags_ == nullptr) {
    intercepted_flags_ = &scratch_flags_;
    *intercepted_flags_ = 0;
  }
  intercepted_on_complete_ = std::exchange(
      batch->payload->recv_message.recv_message_ready, &on_complete_);
}

void BaseCallData::ReceiveMessage::GotPipe(PipeSender<MessageHandle>* sender) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s ReceiveMessage.GotPipe st=%s",
            base_->LogTag().c_str(), StateString(state_));
  }
  switch (state_) {
    case State::kInitial:
      state_ = State::kIdle;
      break;
    case State::kForwardedBatchNoPipe:
      state_ = State::kForwardedBatch;
      break;
    case State::kBatchCompletedNoPipe:
      state_ = State::kBatchCompleted;
      Activity::current()->ForceImmediateRepoll();
      break;
    case State::kIdle:
    case State::kForwardedBatch:
    case State::kBatchCompleted:
    case State::kPushedToPipe:
    case State::kPulledFromPipe:
    case State::kCancelledWhilstForwarding:
    case State::kBatchCompletedButCancelled:
      abort();
    case State::kCancelled:
      return;
  }
  sender_ = sender;
}

void BaseCallData::ReceiveMessage::OnComplete(absl::Status status) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s ReceiveMessage.OnComplete st=%s status=%s",
            base_->LogTag().c_str(), StateString(state_),
            status.ToString().c_str());
  }
  switch (state_) {
    case State::kInitial:
    case State::kIdle:
    case State::kPushedToPipe:
    case State::kPulledFromPipe:
    case State::kBatchCompleted:
    case State::kBatchCompletedNoPipe:
    case State::kCancelled:
    case State::kBatchCompletedButCancelled:
      abort();
    case State::kForwardedBatchNoPipe:
      state_ = State::kBatchCompletedNoPipe;
      return;
    case State::kForwardedBatch:
      state_ = State::kBatchCompleted;
      break;
    case State::kCancelledWhilstForwarding:
      state_ = State::kBatchCompletedButCancelled;
      break;
  }
  completed_status_ = status;
  Flusher flusher(base_);
  ScopedContext ctx(base_);
  base_->WakeInsideCombiner(&flusher);
}

void BaseCallData::ReceiveMessage::Done(const ServerMetadata& metadata,
                                        Flusher* flusher) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s ReceiveMessage.Done st=%s md=%s",
            base_->LogTag().c_str(), StateString(state_),
            metadata.DebugString().c_str());
  }
  switch (state_) {
    case State::kInitial:
    case State::kIdle:
      state_ = State::kCancelled;
      break;
    case State::kForwardedBatch:
    case State::kForwardedBatchNoPipe:
      state_ = State::kCancelledWhilstForwarding;
      break;
    case State::kPulledFromPipe:
    case State::kPushedToPipe: {
      auto status_code =
          metadata.get(GrpcStatusMetadata()).value_or(GRPC_STATUS_OK);
      GPR_ASSERT(status_code != GRPC_STATUS_OK);
      push_.reset();
      next_.reset();
      flusher->AddClosure(intercepted_on_complete_,
                          StatusFromMetadata(metadata), "recv_message_done");
      state_ = State::kCancelled;
    } break;
    case State::kBatchCompleted:
    case State::kBatchCompletedNoPipe:
    case State::kBatchCompletedButCancelled:
      abort();
    case State::kCancelledWhilstForwarding:
    case State::kCancelled:
      break;
  }
}

void BaseCallData::ReceiveMessage::WakeInsideCombiner(Flusher* flusher) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s ReceiveMessage.WakeInsideCombiner st=%s",
            base_->LogTag().c_str(), StateString(state_));
  }
  switch (state_) {
    case State::kInitial:
    case State::kIdle:
    case State::kForwardedBatchNoPipe:
    case State::kForwardedBatch:
    case State::kCancelled:
    case State::kCancelledWhilstForwarding:
    case State::kBatchCompletedNoPipe:
      break;
    case State::kBatchCompletedButCancelled:
      sender_->Close();
      state_ = State::kCancelled;
      flusher->AddClosure(std::exchange(intercepted_on_complete_, nullptr),
                          completed_status_, "recv_message");
      break;
    case State::kBatchCompleted:
      if (completed_status_.ok() && intercepted_slice_buffer_->has_value()) {
        state_ = State::kPushedToPipe;
        auto message = GetContext<Arena>()->MakePooled<Message>();
        message->payload()->Swap(&**intercepted_slice_buffer_);
        message->mutable_flags() = *intercepted_flags_;
        push_ = sender_->Push(std::move(message));
        next_ = pipe_.receiver.Next();
      } else {
        sender_->Close();
        state_ = State::kCancelled;
        flusher->AddClosure(std::exchange(intercepted_on_complete_, nullptr),
                            completed_status_, "recv_message");
        break;
      }
      GPR_ASSERT(state_ == State::kPushedToPipe);
      ABSL_FALLTHROUGH_INTENDED;
    case State::kPushedToPipe: {
      GPR_ASSERT(push_.has_value());
      auto r_push = (*push_)();
      if (auto* p = absl::get_if<bool>(&r_push)) {
        // We haven't pulled through yet, so this certainly shouldn't succeed.
        GPR_ASSERT(!*p);
        state_ = State::kCancelled;
        break;
      }
      GPR_ASSERT(next_.has_value());
      auto r_next = (*next_)();
      if (auto* p = absl::get_if<NextResult<MessageHandle>>(&r_next)) {
        next_.reset();
        if (p->has_value()) {
          *intercepted_slice_buffer_ = std::move(*(**p)->payload());
          *intercepted_flags_ = (**p)->flags();
          state_ = State::kPulledFromPipe;
        } else {
          *intercepted_slice_buffer_ = absl::nullopt;
          *intercepted_flags_ = 0;
          state_ = State::kCancelled;
        }
      }
    }
      if (state_ != State::kPulledFromPipe) break;
      ABSL_FALLTHROUGH_INTENDED;
    case State::kPulledFromPipe: {
      GPR_ASSERT(push_.has_value());
      if (!absl::holds_alternative<Pending>((*push_)())) {
        state_ = State::kIdle;
        push_.reset();
        flusher->AddClosure(std::exchange(intercepted_on_complete_, nullptr),
                            absl::OkStatus(), "recv_message");
      }
      break;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// ClientCallData

struct ClientCallData::RecvInitialMetadata final {
  enum State {
    // Initial state; no op seen
    kInitial,
    // No op seen, but we have a latch that would like to modify it when we do
    kGotLatch,
    // Responded to trailing metadata prior to getting a recv_initial_metadata
    kRespondedToTrailingMetadataPriorToHook,
    // Hooked, no latch yet
    kHookedWaitingForLatch,
    // Hooked, latch seen
    kHookedAndGotLatch,
    // Got the callback, haven't set latch yet
    kCompleteWaitingForLatch,
    // Got the callback and got the latch
    kCompleteAndGotLatch,
    // Got the callback and set the latch
    kCompleteAndSetLatch,
    // Called the original callback
    kResponded,
    // Called the original callback with an error: still need to set the latch
    kRespondedButNeedToSetLatch,
  };

  State state = kInitial;
  grpc_closure* original_on_ready = nullptr;
  grpc_closure on_ready;
  grpc_metadata_batch* metadata = nullptr;
  Latch<ServerMetadata*>* server_initial_metadata_publisher = nullptr;

  static const char* StateString(State state) {
    switch (state) {
      case kInitial:
        return "INITIAL";
      case kGotLatch:
        return "GOT_LATCH";
      case kRespondedToTrailingMetadataPriorToHook:
        return "RESPONDED_TO_TRAILING_METADATA_PRIOR_TO_HOOK";
      case kHookedWaitingForLatch:
        return "HOOKED_WAITING_FOR_LATCH";
      case kHookedAndGotLatch:
        return "HOOKED_AND_GOT_LATCH";
      case kCompleteWaitingForLatch:
        return "COMPLETE_WAITING_FOR_LATCH";
      case kCompleteAndGotLatch:
        return "COMPLETE_AND_GOT_LATCH";
      case kCompleteAndSetLatch:
        return "COMPLETE_AND_SET_LATCH";
      case kResponded:
        return "RESPONDED";
      case kRespondedButNeedToSetLatch:
        return "RESPONDED_BUT_NEED_TO_SET_LATCH";
    }
    return "UNKNOWN";
  }
};

class ClientCallData::PollContext {
 public:
  explicit PollContext(ClientCallData* self, Flusher* flusher)
      : self_(self), flusher_(flusher) {
    GPR_ASSERT(self_->poll_ctx_ == nullptr);

    self_->poll_ctx_ = this;
    scoped_activity_.Init(self_);
    have_scoped_activity_ = true;
  }

  PollContext(const PollContext&) = delete;
  PollContext& operator=(const PollContext&) = delete;

  void Run() {
    if (grpc_trace_channel.enabled()) {
      gpr_log(GPR_DEBUG, "%s ClientCallData.PollContext.Run %s",
              self_->LogTag().c_str(), self_->DebugString().c_str());
    }
    GPR_ASSERT(have_scoped_activity_);
    repoll_ = false;
    if (self_->send_message() != nullptr) {
      self_->send_message()->WakeInsideCombiner(flusher_);
    }
    if (self_->receive_message() != nullptr) {
      self_->receive_message()->WakeInsideCombiner(flusher_);
    }
    if (self_->server_initial_metadata_latch() != nullptr) {
      switch (self_->recv_initial_metadata_->state) {
        case RecvInitialMetadata::kInitial:
        case RecvInitialMetadata::kGotLatch:
        case RecvInitialMetadata::kHookedWaitingForLatch:
        case RecvInitialMetadata::kHookedAndGotLatch:
        case RecvInitialMetadata::kCompleteWaitingForLatch:
        case RecvInitialMetadata::kResponded:
        case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
          break;
        case RecvInitialMetadata::kRespondedButNeedToSetLatch:
          self_->recv_initial_metadata_->server_initial_metadata_publisher->Set(
              nullptr);
          self_->recv_initial_metadata_->state =
              RecvInitialMetadata::kResponded;
          break;
        case RecvInitialMetadata::kCompleteAndGotLatch:
          self_->recv_initial_metadata_->state =
              RecvInitialMetadata::kCompleteAndSetLatch;
          self_->recv_initial_metadata_->server_initial_metadata_publisher->Set(
              self_->recv_initial_metadata_->metadata);
          ABSL_FALLTHROUGH_INTENDED;
        case RecvInitialMetadata::kCompleteAndSetLatch: {
          Poll<ServerMetadata**> p =
              self_->server_initial_metadata_latch()->Wait()();
          if (ServerMetadata*** ppp = absl::get_if<ServerMetadata**>(&p)) {
            ServerMetadata* md = **ppp;
            if (self_->recv_initial_metadata_->metadata != md) {
              *self_->recv_initial_metadata_->metadata = std::move(*md);
            }
            self_->recv_initial_metadata_->state =
                RecvInitialMetadata::kResponded;
            flusher_->AddClosure(
                std::exchange(self_->recv_initial_metadata_->original_on_ready,
                              nullptr),
                absl::OkStatus(),
                "wake_inside_combiner:recv_initial_metadata_ready");
          }
        } break;
      }
    }
    if (self_->recv_trailing_state_ == RecvTrailingState::kCancelled ||
        self_->recv_trailing_state_ == RecvTrailingState::kResponded) {
      return;
    }
    switch (self_->send_initial_state_) {
      case SendInitialState::kQueued:
      case SendInitialState::kForwarded: {
        // Poll the promise once since we're waiting for it.
        Poll<ServerMetadataHandle> poll = self_->promise_();
        if (grpc_trace_channel.enabled()) {
          gpr_log(GPR_DEBUG, "%s ClientCallData.PollContext.Run: poll=%s",
                  self_->LogTag().c_str(),
                  PollToString(poll, [](const ServerMetadataHandle& h) {
                    return h->DebugString();
                  }).c_str());
        }
        if (auto* r = absl::get_if<ServerMetadataHandle>(&poll)) {
          auto md = std::move(*r);
          if (self_->send_message() != nullptr) {
            self_->send_message()->Done(*md);
          }
          if (self_->receive_message() != nullptr) {
            self_->receive_message()->Done(*md, flusher_);
          }
          if (self_->recv_trailing_state_ == RecvTrailingState::kComplete) {
            if (self_->recv_trailing_metadata_ != md.get()) {
              *self_->recv_trailing_metadata_ = std::move(*md);
            }
            self_->recv_trailing_state_ = RecvTrailingState::kResponded;
            flusher_->AddClosure(
                std::exchange(self_->original_recv_trailing_metadata_ready_,
                              nullptr),
                absl::OkStatus(), "wake_inside_combiner:recv_trailing_ready:1");
            if (self_->recv_initial_metadata_ != nullptr) {
              switch (self_->recv_initial_metadata_->state) {
                case RecvInitialMetadata::kInitial:
                case RecvInitialMetadata::kGotLatch:
                  self_->recv_initial_metadata_->state = RecvInitialMetadata::
                      kRespondedToTrailingMetadataPriorToHook;
                  break;
                case RecvInitialMetadata::
                    kRespondedToTrailingMetadataPriorToHook:
                case RecvInitialMetadata::kRespondedButNeedToSetLatch:
                  abort();  // not reachable
                  break;
                case RecvInitialMetadata::kHookedWaitingForLatch:
                case RecvInitialMetadata::kHookedAndGotLatch:
                case RecvInitialMetadata::kResponded:
                case RecvInitialMetadata::kCompleteAndGotLatch:
                case RecvInitialMetadata::kCompleteAndSetLatch:
                  break;
                case RecvInitialMetadata::kCompleteWaitingForLatch:
                  self_->recv_initial_metadata_->state =
                      RecvInitialMetadata::kResponded;
                  flusher_->AddClosure(
                      std::exchange(
                          self_->recv_initial_metadata_->original_on_ready,
                          nullptr),
                      absl::CancelledError(),
                      "wake_inside_combiner:recv_initial_metadata_ready");
              }
            }
          } else {
            self_->cancelled_error_ = StatusFromMetadata(*md);
            GPR_ASSERT(!self_->cancelled_error_.ok());
            if (self_->recv_initial_metadata_ != nullptr) {
              switch (self_->recv_initial_metadata_->state) {
                case RecvInitialMetadata::kInitial:
                case RecvInitialMetadata::kGotLatch:
                  self_->recv_initial_metadata_->state = RecvInitialMetadata::
                      kRespondedToTrailingMetadataPriorToHook;
                  break;
                case RecvInitialMetadata::kHookedWaitingForLatch:
                case RecvInitialMetadata::kHookedAndGotLatch:
                case RecvInitialMetadata::kResponded:
                  break;
                case RecvInitialMetadata::
                    kRespondedToTrailingMetadataPriorToHook:
                case RecvInitialMetadata::kRespondedButNeedToSetLatch:
                  abort();  // not reachable
                  break;
                case RecvInitialMetadata::kCompleteWaitingForLatch:
                case RecvInitialMetadata::kCompleteAndGotLatch:
                case RecvInitialMetadata::kCompleteAndSetLatch:
                  self_->recv_initial_metadata_->state =
                      RecvInitialMetadata::kResponded;
                  flusher_->AddClosure(
                      std::exchange(
                          self_->recv_initial_metadata_->original_on_ready,
                          nullptr),
                      self_->cancelled_error_,
                      "wake_inside_combiner:recv_initial_metadata_ready");
              }
            }
            if (self_->send_initial_state_ == SendInitialState::kQueued) {
              self_->send_initial_state_ = SendInitialState::kCancelled;
              self_->send_initial_metadata_batch_.CancelWith(
                  self_->cancelled_error_, flusher_);
            } else {
              GPR_ASSERT(
                  self_->recv_trailing_state_ == RecvTrailingState::kInitial ||
                  self_->recv_trailing_state_ == RecvTrailingState::kForwarded);
              self_->call_combiner()->Cancel(self_->cancelled_error_);
              CapturedBatch b(grpc_make_transport_stream_op(GRPC_CLOSURE_CREATE(
                  [](void* p, grpc_error_handle) {
                    GRPC_CALL_COMBINER_STOP(static_cast<CallCombiner*>(p),
                                            "finish_cancel");
                  },
                  self_->call_combiner(), nullptr)));
              b->cancel_stream = true;
              b->payload->cancel_stream.cancel_error = self_->cancelled_error_;
              b.ResumeWith(flusher_);
            }
            self_->cancelling_metadata_ = std::move(md);
            self_->recv_trailing_state_ = RecvTrailingState::kCancelled;
          }
          self_->promise_ = ArenaPromise<ServerMetadataHandle>();
          scoped_activity_.Destroy();
          have_scoped_activity_ = false;
        }
      } break;
      case SendInitialState::kInitial:
      case SendInitialState::kCancelled:
        // If we get a response without sending anything, we just propagate
        // that up. (note: that situation isn't possible once we finish the
        // promise transition).
        if (self_->recv_trailing_state_ == RecvTrailingState::kComplete) {
          self_->recv_trailing_state_ = RecvTrailingState::kResponded;
          flusher_->AddClosure(
              std::exchange(self_->original_recv_trailing_metadata_ready_,
                            nullptr),
              absl::OkStatus(), "wake_inside_combiner:recv_trailing_ready:2");
        }
        break;
    }
  }

  ~PollContext() {
    self_->poll_ctx_ = nullptr;
    if (have_scoped_activity_) scoped_activity_.Destroy();
    if (repoll_) {
      struct NextPoll : public grpc_closure {
        grpc_call_stack* call_stack;
        ClientCallData* call_data;
      };
      auto run = [](void* p, grpc_error_handle) {
        auto* next_poll = static_cast<NextPoll*>(p);
        {
          Flusher flusher(next_poll->call_data);
          next_poll->call_data->WakeInsideCombiner(&flusher);
        }
        GRPC_CALL_STACK_UNREF(next_poll->call_stack, "re-poll");
        delete next_poll;
      };
      // Unique ptr --> release to suppress clang-tidy warnings about allocating
      // in a destructor.
      auto* p = std::make_unique<NextPoll>().release();
      p->call_stack = self_->call_stack();
      p->call_data = self_;
      GRPC_CALL_STACK_REF(self_->call_stack(), "re-poll");
      GRPC_CLOSURE_INIT(p, run, p, nullptr);
      flusher_->AddClosure(p, absl::OkStatus(), "re-poll");
    }
  }

  void Repoll() { repoll_ = true; }

  void ForwardSendInitialMetadata() {
    self_->send_initial_metadata_batch_.ResumeWith(flusher_);
  }

 private:
  ManualConstructor<ScopedActivity> scoped_activity_;
  ClientCallData* self_;
  Flusher* flusher_;
  bool repoll_ = false;
  bool have_scoped_activity_;
};

ClientCallData::ClientCallData(grpc_call_element* elem,
                               const grpc_call_element_args* args,
                               uint8_t flags)
    : BaseCallData(elem, args, flags) {
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                    RecvTrailingMetadataReadyCallback, this,
                    grpc_schedule_on_exec_ctx);
  if (server_initial_metadata_latch() != nullptr) {
    recv_initial_metadata_ = arena()->New<RecvInitialMetadata>();
  }
}

ClientCallData::~ClientCallData() {
  GPR_ASSERT(poll_ctx_ == nullptr);
  if (recv_initial_metadata_ != nullptr) {
    recv_initial_metadata_->~RecvInitialMetadata();
  }
}

// Activity implementation.
void ClientCallData::ForceImmediateRepoll() {
  GPR_ASSERT(poll_ctx_ != nullptr);
  poll_ctx_->Repoll();
}

const char* ClientCallData::StateString(SendInitialState state) {
  switch (state) {
    case SendInitialState::kInitial:
      return "INITIAL";
    case SendInitialState::kQueued:
      return "QUEUED";
    case SendInitialState::kForwarded:
      return "FORWARDED";
    case SendInitialState::kCancelled:
      return "CANCELLED";
  }
  return "UNKNOWN";
}

const char* ClientCallData::StateString(RecvTrailingState state) {
  switch (state) {
    case RecvTrailingState::kInitial:
      return "INITIAL";
    case RecvTrailingState::kQueued:
      return "QUEUED";
    case RecvTrailingState::kComplete:
      return "COMPLETE";
    case RecvTrailingState::kForwarded:
      return "FORWARDED";
    case RecvTrailingState::kCancelled:
      return "CANCELLED";
    case RecvTrailingState::kResponded:
      return "RESPONDED";
  }
  return "UNKNOWN";
}

std::string ClientCallData::DebugString() const {
  std::vector<absl::string_view> captured;
  if (send_initial_metadata_batch_.is_captured()) {
    captured.push_back("send_initial_metadata");
  }
  if (send_message() != nullptr && send_message()->HaveCapturedBatch()) {
    captured.push_back("send_message");
  }
  return absl::StrCat(
      "has_promise=", promise_.has_value() ? "true" : "false",
      " sent_initial_state=", StateString(send_initial_state_),
      " recv_trailing_state=", StateString(recv_trailing_state_), " captured={",
      absl::StrJoin(captured, ","), "}",
      server_initial_metadata_latch() == nullptr
          ? ""
          : absl::StrCat(" recv_initial_metadata=",
                         RecvInitialMetadata::StateString(
                             recv_initial_metadata_->state)));
}

// Handle one grpc_transport_stream_op_batch
void ClientCallData::StartBatch(grpc_transport_stream_op_batch* b) {
  // Fake out the activity based context.
  ScopedContext context(this);
  CapturedBatch batch(b);
  Flusher flusher(this);

  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s StartBatch %s", LogTag().c_str(),
            DebugString().c_str());
  }

  // If this is a cancel stream, cancel anything we have pending and propagate
  // the cancellation.
  if (batch->cancel_stream) {
    GPR_ASSERT(!batch->send_initial_metadata &&
               !batch->send_trailing_metadata && !batch->send_message &&
               !batch->recv_initial_metadata && !batch->recv_message &&
               !batch->recv_trailing_metadata);
    PollContext poll_ctx(this, &flusher);
    Cancel(batch->payload->cancel_stream.cancel_error, &flusher);
    poll_ctx.Run();
    if (is_last()) {
      batch.CompleteWith(&flusher);
    } else {
      batch.ResumeWith(&flusher);
    }
    return;
  }

  if (recv_initial_metadata_ != nullptr && batch->recv_initial_metadata) {
    bool hook = true;
    switch (recv_initial_metadata_->state) {
      case RecvInitialMetadata::kInitial:
        recv_initial_metadata_->state =
            RecvInitialMetadata::kHookedWaitingForLatch;
        break;
      case RecvInitialMetadata::kGotLatch:
        recv_initial_metadata_->state = RecvInitialMetadata::kHookedAndGotLatch;
        break;
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
        hook = false;
        break;
      case RecvInitialMetadata::kHookedWaitingForLatch:
      case RecvInitialMetadata::kHookedAndGotLatch:
      case RecvInitialMetadata::kCompleteWaitingForLatch:
      case RecvInitialMetadata::kCompleteAndGotLatch:
      case RecvInitialMetadata::kCompleteAndSetLatch:
      case RecvInitialMetadata::kResponded:
      case RecvInitialMetadata::kRespondedButNeedToSetLatch:
        abort();  // unreachable
    }
    if (hook) {
      auto cb = [](void* ptr, grpc_error_handle error) {
        ClientCallData* self = static_cast<ClientCallData*>(ptr);
        self->RecvInitialMetadataReady(error);
      };
      recv_initial_metadata_->metadata =
          batch->payload->recv_initial_metadata.recv_initial_metadata;
      recv_initial_metadata_->original_on_ready =
          batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
      GRPC_CLOSURE_INIT(&recv_initial_metadata_->on_ready, cb, this, nullptr);
      batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
          &recv_initial_metadata_->on_ready;
    }
  }

  bool wake = false;
  if (send_message() != nullptr && batch->send_message) {
    send_message()->StartOp(batch);
    wake = true;
  }
  if (receive_message() != nullptr && batch->recv_message) {
    receive_message()->StartOp(batch);
    wake = true;
  }

  // send_initial_metadata: seeing this triggers the start of the promise part
  // of this filter.
  if (batch->send_initial_metadata) {
    // If we're already cancelled, just terminate the batch.
    if (send_initial_state_ == SendInitialState::kCancelled ||
        recv_trailing_state_ == RecvTrailingState::kCancelled) {
      batch.CancelWith(cancelled_error_, &flusher);
    } else {
      // Otherwise, we should not have seen a send_initial_metadata op yet.
      GPR_ASSERT(send_initial_state_ == SendInitialState::kInitial);
      // Mark ourselves as queued.
      send_initial_state_ = SendInitialState::kQueued;
      if (batch->recv_trailing_metadata) {
        // If there's a recv_trailing_metadata op, we queue that too.
        GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kInitial);
        recv_trailing_state_ = RecvTrailingState::kQueued;
      }
      // This is the queuing!
      send_initial_metadata_batch_ = batch;
      // And kick start the promise.
      StartPromise(&flusher);
      wake = false;
    }
  } else if (batch->recv_trailing_metadata) {
    // recv_trailing_metadata *without* send_initial_metadata: hook it so we
    // can respond to it, and push it down.
    if (recv_trailing_state_ == RecvTrailingState::kCancelled) {
      batch.CancelWith(cancelled_error_, &flusher);
    } else {
      GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kInitial);
      recv_trailing_state_ = RecvTrailingState::kForwarded;
      HookRecvTrailingMetadata(batch);
    }
  } else if (!cancelled_error_.ok()) {
    batch.CancelWith(cancelled_error_, &flusher);
  }

  if (wake) {
    PollContext(this, &flusher).Run();
  }

  if (batch.is_captured()) {
    if (!is_last()) {
      batch.ResumeWith(&flusher);
    } else {
      batch.CancelWith(absl::CancelledError(), &flusher);
    }
  }
}

// Handle cancellation.
void ClientCallData::Cancel(grpc_error_handle error, Flusher* flusher) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s Cancel error=%s", LogTag().c_str(),
            error.ToString().c_str());
  }
  // Track the latest reason for cancellation.
  cancelled_error_ = error;
  // Stop running the promise.
  promise_ = ArenaPromise<ServerMetadataHandle>();
  // If we have an op queued, fail that op.
  // Record what we've done.
  if (send_initial_state_ == SendInitialState::kQueued) {
    send_initial_state_ = SendInitialState::kCancelled;
    if (recv_trailing_state_ == RecvTrailingState::kQueued) {
      recv_trailing_state_ = RecvTrailingState::kCancelled;
    }
    send_initial_metadata_batch_.CancelWith(error, flusher);
  } else {
    send_initial_state_ = SendInitialState::kCancelled;
  }
  if (recv_initial_metadata_ != nullptr) {
    switch (recv_initial_metadata_->state) {
      case RecvInitialMetadata::kCompleteWaitingForLatch:
      case RecvInitialMetadata::kCompleteAndGotLatch:
      case RecvInitialMetadata::kCompleteAndSetLatch:
        recv_initial_metadata_->state = RecvInitialMetadata::kResponded;
        GRPC_CALL_COMBINER_START(
            call_combiner(),
            std::exchange(recv_initial_metadata_->original_on_ready, nullptr),
            error, "propagate cancellation");
        break;
      case RecvInitialMetadata::kInitial:
      case RecvInitialMetadata::kGotLatch:
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
      case RecvInitialMetadata::kHookedWaitingForLatch:
      case RecvInitialMetadata::kHookedAndGotLatch:
      case RecvInitialMetadata::kResponded:
        break;
      case RecvInitialMetadata::kRespondedButNeedToSetLatch:
        abort();
        break;
    }
  }
  if (send_message() != nullptr) {
    send_message()->Done(*ServerMetadataFromStatus(error));
  }
  if (receive_message() != nullptr) {
    receive_message()->Done(*ServerMetadataFromStatus(error), flusher);
  }
}

// Begin running the promise - which will ultimately take some initial
// metadata and return some trailing metadata.
void ClientCallData::StartPromise(Flusher* flusher) {
  GPR_ASSERT(send_initial_state_ == SendInitialState::kQueued);
  ChannelFilter* filter = static_cast<ChannelFilter*>(elem()->channel_data);

  // Construct the promise.
  PollContext ctx(this, flusher);
  promise_ = filter->MakeCallPromise(
      CallArgs{WrapMetadata(send_initial_metadata_batch_->payload
                                ->send_initial_metadata.send_initial_metadata),
               server_initial_metadata_latch(), outgoing_messages_pipe(),
               incoming_messages_pipe()},
      [this](CallArgs call_args) {
        return MakeNextPromise(std::move(call_args));
      });
  ctx.Run();
}

void ClientCallData::RecvInitialMetadataReady(grpc_error_handle error) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s ClientCallData.RecvInitialMetadataReady %s",
            LogTag().c_str(), DebugString().c_str());
  }
  ScopedContext context(this);
  Flusher flusher(this);
  if (!error.ok()) {
    switch (recv_initial_metadata_->state) {
      case RecvInitialMetadata::kHookedWaitingForLatch:
        recv_initial_metadata_->state = RecvInitialMetadata::kResponded;
        break;
      case RecvInitialMetadata::kHookedAndGotLatch:
        recv_initial_metadata_->state =
            RecvInitialMetadata::kRespondedButNeedToSetLatch;
        break;
      case RecvInitialMetadata::kInitial:
      case RecvInitialMetadata::kGotLatch:
      case RecvInitialMetadata::kCompleteWaitingForLatch:
      case RecvInitialMetadata::kCompleteAndGotLatch:
      case RecvInitialMetadata::kCompleteAndSetLatch:
      case RecvInitialMetadata::kResponded:
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
      case RecvInitialMetadata::kRespondedButNeedToSetLatch:
        abort();  // unreachable
    }
    flusher.AddClosure(
        std::exchange(recv_initial_metadata_->original_on_ready, nullptr),
        error, "propagate cancellation");
  } else if (send_initial_state_ == SendInitialState::kCancelled ||
             recv_trailing_state_ == RecvTrailingState::kResponded) {
    recv_initial_metadata_->state = RecvInitialMetadata::kResponded;
    flusher.AddClosure(
        std::exchange(recv_initial_metadata_->original_on_ready, nullptr),
        cancelled_error_, "propagate cancellation");
  } else {
    switch (recv_initial_metadata_->state) {
      case RecvInitialMetadata::kHookedWaitingForLatch:
        recv_initial_metadata_->state =
            RecvInitialMetadata::kCompleteWaitingForLatch;
        break;
      case RecvInitialMetadata::kHookedAndGotLatch:
        recv_initial_metadata_->state =
            RecvInitialMetadata::kCompleteAndGotLatch;
        break;
      case RecvInitialMetadata::kInitial:
      case RecvInitialMetadata::kGotLatch:
      case RecvInitialMetadata::kCompleteWaitingForLatch:
      case RecvInitialMetadata::kCompleteAndGotLatch:
      case RecvInitialMetadata::kCompleteAndSetLatch:
      case RecvInitialMetadata::kResponded:
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
      case RecvInitialMetadata::kRespondedButNeedToSetLatch:
        abort();  // unreachable
    }
  }
  WakeInsideCombiner(&flusher);
}

// Interject our callback into the op batch for recv trailing metadata ready.
// Stash a pointer to the trailing metadata that will be filled in, so we can
// manipulate it later.
void ClientCallData::HookRecvTrailingMetadata(CapturedBatch batch) {
  recv_trailing_metadata_ =
      batch->payload->recv_trailing_metadata.recv_trailing_metadata;
  original_recv_trailing_metadata_ready_ =
      batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
  batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      &recv_trailing_metadata_ready_;
}

// Construct a promise that will "call" the next filter.
// Effectively:
//   - put the modified initial metadata into the batch to be sent down.
//   - return a wrapper around PollTrailingMetadata as the promise.
ArenaPromise<ServerMetadataHandle> ClientCallData::MakeNextPromise(
    CallArgs call_args) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s ClientCallData.MakeNextPromise %s", LogTag().c_str(),
            DebugString().c_str());
  }
  GPR_ASSERT(poll_ctx_ != nullptr);
  GPR_ASSERT(send_initial_state_ == SendInitialState::kQueued);
  send_initial_metadata_batch_->payload->send_initial_metadata
      .send_initial_metadata =
      UnwrapMetadata(std::move(call_args.client_initial_metadata));
  if (recv_initial_metadata_ != nullptr) {
    // Call args should contain a latch for receiving initial metadata.
    // It might be the one we passed in - in which case we know this filter
    // only wants to examine the metadata, or it might be a new instance, in
    // which case we know the filter wants to mutate.
    GPR_ASSERT(call_args.server_initial_metadata != nullptr);
    recv_initial_metadata_->server_initial_metadata_publisher =
        call_args.server_initial_metadata;
    switch (recv_initial_metadata_->state) {
      case RecvInitialMetadata::kInitial:
        recv_initial_metadata_->state = RecvInitialMetadata::kGotLatch;
        break;
      case RecvInitialMetadata::kHookedWaitingForLatch:
        recv_initial_metadata_->state = RecvInitialMetadata::kHookedAndGotLatch;
        poll_ctx_->Repoll();
        break;
      case RecvInitialMetadata::kCompleteWaitingForLatch:
        recv_initial_metadata_->state =
            RecvInitialMetadata::kCompleteAndGotLatch;
        poll_ctx_->Repoll();
        break;
      case RecvInitialMetadata::kGotLatch:
      case RecvInitialMetadata::kHookedAndGotLatch:
      case RecvInitialMetadata::kCompleteAndGotLatch:
      case RecvInitialMetadata::kCompleteAndSetLatch:
      case RecvInitialMetadata::kResponded:
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
      case RecvInitialMetadata::kRespondedButNeedToSetLatch:
        abort();  // unreachable
    }
  } else {
    GPR_ASSERT(call_args.server_initial_metadata == nullptr);
  }
  if (send_message() != nullptr) {
    send_message()->GotPipe(call_args.outgoing_messages);
  } else {
    GPR_ASSERT(call_args.outgoing_messages == nullptr);
  }
  if (receive_message() != nullptr) {
    receive_message()->GotPipe(call_args.incoming_messages);
  } else {
    GPR_ASSERT(call_args.incoming_messages == nullptr);
  }
  return ArenaPromise<ServerMetadataHandle>(
      [this]() { return PollTrailingMetadata(); });
}

// Wrapper to make it look like we're calling the next filter as a promise.
// First poll: send the send_initial_metadata op down the stack.
// All polls: await receiving the trailing metadata, then return it to the
// application.
Poll<ServerMetadataHandle> ClientCallData::PollTrailingMetadata() {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s ClientCallData.PollTrailingMetadata %s",
            LogTag().c_str(), DebugString().c_str());
  }
  GPR_ASSERT(poll_ctx_ != nullptr);
  if (send_initial_state_ == SendInitialState::kQueued) {
    // First poll: pass the send_initial_metadata op down the stack.
    GPR_ASSERT(send_initial_metadata_batch_.is_captured());
    send_initial_state_ = SendInitialState::kForwarded;
    if (recv_trailing_state_ == RecvTrailingState::kQueued) {
      // (and the recv_trailing_metadata op if it's part of the queuing)
      HookRecvTrailingMetadata(send_initial_metadata_batch_);
      recv_trailing_state_ = RecvTrailingState::kForwarded;
    }
    poll_ctx_->ForwardSendInitialMetadata();
  }
  switch (recv_trailing_state_) {
    case RecvTrailingState::kInitial:
    case RecvTrailingState::kQueued:
    case RecvTrailingState::kForwarded:
      // No trailing metadata yet: we are pending.
      // We return that and expect the promise to be repolled later (if it's
      // not cancelled).
      return Pending{};
    case RecvTrailingState::kComplete:
      // We've received trailing metadata: pass it to the promise and allow it
      // to adjust it.
      return WrapMetadata(recv_trailing_metadata_);
    case RecvTrailingState::kCancelled: {
      // We've been cancelled: synthesize some trailing metadata and pass it
      // to the calling promise for adjustment.
      recv_trailing_metadata_->Clear();
      SetStatusFromError(recv_trailing_metadata_, cancelled_error_);
      return WrapMetadata(recv_trailing_metadata_);
    }
    case RecvTrailingState::kResponded:
      // We've already responded to the caller: we can't do anything and we
      // should never reach here.
      abort();
  }
  GPR_UNREACHABLE_CODE(return Pending{});
}

void ClientCallData::RecvTrailingMetadataReadyCallback(
    void* arg, grpc_error_handle error) {
  static_cast<ClientCallData*>(arg)->RecvTrailingMetadataReady(error);
}

void ClientCallData::RecvTrailingMetadataReady(grpc_error_handle error) {
  Flusher flusher(this);
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG,
            "%s ClientCallData.RecvTrailingMetadataReady error=%s md=%s",
            LogTag().c_str(), error.ToString().c_str(),
            recv_trailing_metadata_->DebugString().c_str());
  }
  // If we were cancelled prior to receiving this callback, we should simply
  // forward the callback up with the same error.
  if (recv_trailing_state_ == RecvTrailingState::kCancelled) {
    if (cancelling_metadata_.get() != nullptr) {
      *recv_trailing_metadata_ = std::move(*cancelling_metadata_);
    }
    if (grpc_closure* call_closure =
            std::exchange(original_recv_trailing_metadata_ready_, nullptr)) {
      flusher.AddClosure(call_closure, error, "propagate failure");
    }
    return;
  }
  // If there was an error, we'll put that into the trailing metadata and
  // proceed as if there was not.
  if (!error.ok()) {
    SetStatusFromError(recv_trailing_metadata_, error);
  }
  // Record that we've got the callback.
  GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kForwarded);
  recv_trailing_state_ = RecvTrailingState::kComplete;
  // Repoll the promise.
  ScopedContext context(this);
  WakeInsideCombiner(&flusher);
}

// Given an error, fill in ServerMetadataHandle to represent that error.
void ClientCallData::SetStatusFromError(grpc_metadata_batch* metadata,
                                        grpc_error_handle error) {
  grpc_status_code status_code = GRPC_STATUS_UNKNOWN;
  std::string status_details;
  grpc_error_get_status(error, deadline(), &status_code, &status_details,
                        nullptr, nullptr);
  metadata->Set(GrpcStatusMetadata(), status_code);
  metadata->Set(GrpcMessageMetadata(), Slice::FromCopiedString(status_details));
  metadata->GetOrCreatePointer(GrpcStatusContext())
      ->emplace_back(StatusToString(error));
}

// Wakeup and poll the promise if appropriate.
void ClientCallData::WakeInsideCombiner(Flusher* flusher) {
  PollContext(this, flusher).Run();
}

void ClientCallData::OnWakeup() {
  Flusher flusher(this);
  ScopedContext context(this);
  WakeInsideCombiner(&flusher);
}

///////////////////////////////////////////////////////////////////////////////
// ServerCallData

struct ServerCallData::SendInitialMetadata {
  enum State {
    kInitial,
    kGotLatch,
    kQueuedWaitingForLatch,
    kQueuedAndGotLatch,
    kQueuedAndSetLatch,
    kForwarded,
    kCancelled,
  };
  State state = kInitial;
  CapturedBatch batch;
  Latch<ServerMetadata*>* server_initial_metadata_publisher = nullptr;

  static const char* StateString(State state) {
    switch (state) {
      case kInitial:
        return "INITIAL";
      case kGotLatch:
        return "GOT_LATCH";
      case kQueuedWaitingForLatch:
        return "QUEUED_WAITING_FOR_LATCH";
      case kQueuedAndGotLatch:
        return "QUEUED_AND_GOT_LATCH";
      case kQueuedAndSetLatch:
        return "QUEUED_AND_SET_LATCH";
      case kForwarded:
        return "FORWARDED";
      case kCancelled:
        return "CANCELLED";
    }
    return "UNKNOWN";
  }
};

class ServerCallData::PollContext {
 public:
  explicit PollContext(ServerCallData* self, Flusher* flusher)
      : self_(self), flusher_(flusher) {
    GPR_ASSERT(self_->poll_ctx_ == nullptr);
    self_->poll_ctx_ = this;
    scoped_activity_.Init(self_);
    have_scoped_activity_ = true;
  }

  PollContext(const PollContext&) = delete;
  PollContext& operator=(const PollContext&) = delete;

  ~PollContext() {
    self_->poll_ctx_ = nullptr;
    if (have_scoped_activity_) scoped_activity_.Destroy();
    if (repoll_) {
      struct NextPoll : public grpc_closure {
        grpc_call_stack* call_stack;
        ServerCallData* call_data;
      };
      auto run = [](void* p, grpc_error_handle) {
        auto* next_poll = static_cast<NextPoll*>(p);
        {
          Flusher flusher(next_poll->call_data);
          next_poll->call_data->WakeInsideCombiner(&flusher);
        }
        GRPC_CALL_STACK_UNREF(next_poll->call_stack, "re-poll");
        delete next_poll;
      };
      auto* p = std::make_unique<NextPoll>().release();
      p->call_stack = self_->call_stack();
      p->call_data = self_;
      GRPC_CALL_STACK_REF(self_->call_stack(), "re-poll");
      GRPC_CLOSURE_INIT(p, run, p, nullptr);
      flusher_->AddClosure(p, absl::OkStatus(), "re-poll");
    }
  }

  void Repoll() { repoll_ = true; }
  void ClearRepoll() { repoll_ = false; }

 private:
  ManualConstructor<ScopedActivity> scoped_activity_;
  ServerCallData* const self_;
  Flusher* const flusher_;
  bool repoll_ = false;
  bool have_scoped_activity_;
};

const char* ServerCallData::StateString(RecvInitialState state) {
  switch (state) {
    case RecvInitialState::kInitial:
      return "INITIAL";
    case RecvInitialState::kForwarded:
      return "FORWARDED";
    case RecvInitialState::kComplete:
      return "COMPLETE";
    case RecvInitialState::kResponded:
      return "RESPONDED";
  }
  return "UNKNOWN";
}

const char* ServerCallData::StateString(SendTrailingState state) {
  switch (state) {
    case SendTrailingState::kInitial:
      return "INITIAL";
    case SendTrailingState::kForwarded:
      return "FORWARDED";
    case SendTrailingState::kQueuedBehindSendMessage:
      return "QUEUED_BEHIND_SEND_MESSAGE";
    case SendTrailingState::kQueued:
      return "QUEUED";
    case SendTrailingState::kCancelled:
      return "CANCELLED";
  }
  return "UNKNOWN";
}

ServerCallData::ServerCallData(grpc_call_element* elem,
                               const grpc_call_element_args* args,
                               uint8_t flags)
    : BaseCallData(elem, args, flags) {
  if (server_initial_metadata_latch() != nullptr) {
    send_initial_metadata_ = arena()->New<SendInitialMetadata>();
  }
  GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_,
                    RecvInitialMetadataReadyCallback, this,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                    RecvTrailingMetadataReadyCallback, this,
                    grpc_schedule_on_exec_ctx);
}

ServerCallData::~ServerCallData() {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s ~ServerCallData %s", LogTag().c_str(),
            DebugString().c_str());
  }
  GPR_ASSERT(poll_ctx_ == nullptr);
}

// Activity implementation.
void ServerCallData::ForceImmediateRepoll() {
  GPR_ASSERT(poll_ctx_ != nullptr);
  poll_ctx_->Repoll();
}

// Handle one grpc_transport_stream_op_batch
void ServerCallData::StartBatch(grpc_transport_stream_op_batch* b) {
  // Fake out the activity based context.
  ScopedContext context(this);
  CapturedBatch batch(b);
  Flusher flusher(this);
  bool wake = false;

  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s StartBatch: %s", LogTag().c_str(),
            DebugString().c_str());
  }

  // If this is a cancel stream, cancel anything we have pending and
  // propagate the cancellation.
  if (batch->cancel_stream) {
    GPR_ASSERT(!batch->send_initial_metadata &&
               !batch->send_trailing_metadata && !batch->send_message &&
               !batch->recv_initial_metadata && !batch->recv_message &&
               !batch->recv_trailing_metadata);
    PollContext poll_ctx(this, &flusher);
    Completed(batch->payload->cancel_stream.cancel_error, &flusher);
    if (is_last()) {
      batch.CompleteWith(&flusher);
    } else {
      batch.ResumeWith(&flusher);
    }
    return;
  }

  // recv_initial_metadata: we hook the response of this so we can start the
  // promise at an appropriate time.
  if (batch->recv_initial_metadata) {
    GPR_ASSERT(!batch->send_initial_metadata &&
               !batch->send_trailing_metadata && !batch->send_message &&
               !batch->recv_message && !batch->recv_trailing_metadata);
    // Otherwise, we should not have seen a send_initial_metadata op yet.
    GPR_ASSERT(recv_initial_state_ == RecvInitialState::kInitial);
    // Hook the callback so we know when to start the promise.
    recv_initial_metadata_ =
        batch->payload->recv_initial_metadata.recv_initial_metadata;
    original_recv_initial_metadata_ready_ =
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
    batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &recv_initial_metadata_ready_;
    recv_initial_state_ = RecvInitialState::kForwarded;
  }

  // Hook recv_trailing_metadata so we can see cancellation from the client.
  if (batch->recv_trailing_metadata) {
    recv_trailing_metadata_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata;
    original_recv_trailing_metadata_ready_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &recv_trailing_metadata_ready_;
  }

  // send_initial_metadata
  if (send_initial_metadata_ != nullptr && batch->send_initial_metadata) {
    switch (send_initial_metadata_->state) {
      case SendInitialMetadata::kInitial:
        send_initial_metadata_->state =
            SendInitialMetadata::kQueuedWaitingForLatch;
        break;
      case SendInitialMetadata::kGotLatch:
        send_initial_metadata_->state = SendInitialMetadata::kQueuedAndGotLatch;
        break;
      case SendInitialMetadata::kCancelled:
        batch.CancelWith(
            cancelled_error_.ok() ? absl::CancelledError() : cancelled_error_,
            &flusher);
        break;
      case SendInitialMetadata::kQueuedAndGotLatch:
      case SendInitialMetadata::kQueuedWaitingForLatch:
      case SendInitialMetadata::kQueuedAndSetLatch:
      case SendInitialMetadata::kForwarded:
        abort();  // not reachable
    }
    send_initial_metadata_->batch = batch;
    wake = true;
  }

  if (send_message() != nullptr && batch->send_message) {
    send_message()->StartOp(batch);
    wake = true;
  }
  if (receive_message() != nullptr && batch->recv_message) {
    receive_message()->StartOp(batch);
    wake = true;
  }

  // send_trailing_metadata
  if (batch.is_captured() && batch->send_trailing_metadata) {
    switch (send_trailing_state_) {
      case SendTrailingState::kInitial:
        send_trailing_metadata_batch_ = batch;
        if (send_message() != nullptr && !send_message()->IsIdle()) {
          send_trailing_state_ = SendTrailingState::kQueuedBehindSendMessage;
        } else {
          send_trailing_state_ = SendTrailingState::kQueued;
          wake = true;
        }
        break;
      case SendTrailingState::kQueued:
      case SendTrailingState::kQueuedBehindSendMessage:
      case SendTrailingState::kForwarded:
        abort();  // unreachable
        break;
      case SendTrailingState::kCancelled:
        batch.CancelWith(
            cancelled_error_.ok() ? absl::CancelledError() : cancelled_error_,
            &flusher);
        break;
    }
  }

  if (wake) WakeInsideCombiner(&flusher);
  if (batch.is_captured()) batch.ResumeWith(&flusher);
}

// Handle cancellation.
void ServerCallData::Completed(grpc_error_handle error, Flusher* flusher) {
  // Track the latest reason for cancellation.
  cancelled_error_ = error;
  // Stop running the promise.
  promise_ = ArenaPromise<ServerMetadataHandle>();
  if (send_trailing_state_ == SendTrailingState::kQueued) {
    send_trailing_state_ = SendTrailingState::kCancelled;
    send_trailing_metadata_batch_.CancelWith(error, flusher);
  } else {
    send_trailing_state_ = SendTrailingState::kCancelled;
  }
  if (send_initial_metadata_ != nullptr) {
    switch (send_initial_metadata_->state) {
      case SendInitialMetadata::kInitial:
      case SendInitialMetadata::kGotLatch:
      case SendInitialMetadata::kForwarded:
      case SendInitialMetadata::kCancelled:
        break;
      case SendInitialMetadata::kQueuedWaitingForLatch:
      case SendInitialMetadata::kQueuedAndGotLatch:
      case SendInitialMetadata::kQueuedAndSetLatch:
        send_initial_metadata_->batch.CancelWith(error, flusher);
        break;
    }
    send_initial_metadata_->state = SendInitialMetadata::kCancelled;
  }
  if (auto* closure =
          std::exchange(original_recv_initial_metadata_ready_, nullptr)) {
    flusher->AddClosure(closure, error, "original_recv_initial_metadata");
  }
  ScopedContext ctx(this);
  if (send_message() != nullptr) {
    send_message()->Done(*ServerMetadataFromStatus(error));
  }
  if (receive_message() != nullptr) {
    receive_message()->Done(*ServerMetadataFromStatus(error), flusher);
  }
}

// Construct a promise that will "call" the next filter.
// Effectively:
//   - put the modified initial metadata into the batch being sent up.
//   - return a wrapper around PollTrailingMetadata as the promise.
ArenaPromise<ServerMetadataHandle> ServerCallData::MakeNextPromise(
    CallArgs call_args) {
  GPR_ASSERT(recv_initial_state_ == RecvInitialState::kComplete);
  GPR_ASSERT(UnwrapMetadata(std::move(call_args.client_initial_metadata)) ==
             recv_initial_metadata_);
  forward_recv_initial_metadata_callback_ = true;
  if (send_initial_metadata_ != nullptr) {
    GPR_ASSERT(send_initial_metadata_->server_initial_metadata_publisher ==
               nullptr);
    GPR_ASSERT(call_args.server_initial_metadata != nullptr);
    send_initial_metadata_->server_initial_metadata_publisher =
        call_args.server_initial_metadata;
    switch (send_initial_metadata_->state) {
      case SendInitialMetadata::kInitial:
        send_initial_metadata_->state = SendInitialMetadata::kGotLatch;
        break;
      case SendInitialMetadata::kGotLatch:
      case SendInitialMetadata::kQueuedAndGotLatch:
      case SendInitialMetadata::kQueuedAndSetLatch:
      case SendInitialMetadata::kForwarded:
        abort();  // not reachable
        break;
      case SendInitialMetadata::kQueuedWaitingForLatch:
        send_initial_metadata_->state = SendInitialMetadata::kQueuedAndGotLatch;
        break;
      case SendInitialMetadata::kCancelled:
        break;
    }
  } else {
    GPR_ASSERT(call_args.server_initial_metadata == nullptr);
  }
  if (send_message() != nullptr) {
    send_message()->GotPipe(call_args.outgoing_messages);
  } else {
    GPR_ASSERT(call_args.outgoing_messages == nullptr);
  }
  if (receive_message() != nullptr) {
    receive_message()->GotPipe(call_args.incoming_messages);
  } else {
    GPR_ASSERT(call_args.incoming_messages == nullptr);
  }
  return ArenaPromise<ServerMetadataHandle>(
      [this]() { return PollTrailingMetadata(); });
}

// Wrapper to make it look like we're calling the next filter as a promise.
// All polls: await sending the trailing metadata, then foward it down the
// stack.
Poll<ServerMetadataHandle> ServerCallData::PollTrailingMetadata() {
  switch (send_trailing_state_) {
    case SendTrailingState::kInitial:
    case SendTrailingState::kQueuedBehindSendMessage:
      return Pending{};
    case SendTrailingState::kQueued:
      return WrapMetadata(send_trailing_metadata_batch_->payload
                              ->send_trailing_metadata.send_trailing_metadata);
    case SendTrailingState::kForwarded:
      abort();  // unreachable
    case SendTrailingState::kCancelled:
      // We could translate cancelled_error to metadata and return it... BUT
      // we're not gonna be running much longer and the results going to be
      // ignored.
      return Pending{};
  }
  GPR_UNREACHABLE_CODE(return Pending{});
}

void ServerCallData::RecvTrailingMetadataReadyCallback(
    void* arg, grpc_error_handle error) {
  static_cast<ServerCallData*>(arg)->RecvTrailingMetadataReady(
      std::move(error));
}

void ServerCallData::RecvTrailingMetadataReady(grpc_error_handle error) {
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s: RecvTrailingMetadataReady error=%s md=%s",
            LogTag().c_str(), error.ToString().c_str(),
            recv_trailing_metadata_->DebugString().c_str());
  }
  Flusher flusher(this);
  PollContext poll_ctx(this, &flusher);
  Completed(error, &flusher);
  flusher.AddClosure(original_recv_trailing_metadata_ready_, std::move(error),
                     "continue recv trailing");
}

void ServerCallData::RecvInitialMetadataReadyCallback(void* arg,
                                                      grpc_error_handle error) {
  static_cast<ServerCallData*>(arg)->RecvInitialMetadataReady(std::move(error));
}

void ServerCallData::RecvInitialMetadataReady(grpc_error_handle error) {
  Flusher flusher(this);
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s: RecvInitialMetadataReady %s", LogTag().c_str(),
            error.ToString().c_str());
  }
  GPR_ASSERT(recv_initial_state_ == RecvInitialState::kForwarded);
  // If there was an error we just propagate that through
  if (!error.ok()) {
    recv_initial_state_ = RecvInitialState::kResponded;
    flusher.AddClosure(
        std::exchange(original_recv_initial_metadata_ready_, nullptr), error,
        "propagate error");
    return;
  }
  // Record that we've got the callback.
  recv_initial_state_ = RecvInitialState::kComplete;

  // Start the promise.
  ScopedContext context(this);
  // Construct the promise.
  ChannelFilter* filter = static_cast<ChannelFilter*>(elem()->channel_data);
  FakeActivity().Run([this, filter] {
    promise_ = filter->MakeCallPromise(
        CallArgs{WrapMetadata(recv_initial_metadata_),
                 server_initial_metadata_latch(), outgoing_messages_pipe(),
                 incoming_messages_pipe()},
        [this](CallArgs call_args) {
          return MakeNextPromise(std::move(call_args));
        });
  });
  // Poll once.
  WakeInsideCombiner(&flusher);
  if (auto* closure =
          std::exchange(original_recv_initial_metadata_ready_, nullptr)) {
    flusher.AddClosure(closure, absl::OkStatus(),
                       "original_recv_initial_metadata");
  }
}

std::string ServerCallData::DebugString() const {
  std::vector<absl::string_view> captured;
  if (send_message() != nullptr && send_message()->HaveCapturedBatch()) {
    captured.push_back("send_message");
  }
  if (send_trailing_metadata_batch_.is_captured()) {
    captured.push_back("send_trailing_metadata");
  }
  return absl::StrCat(
      "have_promise=", promise_.has_value() ? "true" : "false",
      " recv_initial_state=", StateString(recv_initial_state_),
      " send_trailing_state=", StateString(send_trailing_state_), " captured={",
      absl::StrJoin(captured, ","), "}",
      send_initial_metadata_ == nullptr
          ? ""
          : absl::StrCat(
                " send_initial_metadata=",
                SendInitialMetadata::StateString(send_initial_metadata_->state))
                .c_str());
}

// Wakeup and poll the promise if appropriate.
void ServerCallData::WakeInsideCombiner(Flusher* flusher) {
  PollContext poll_ctx(this, flusher);
  if (grpc_trace_channel.enabled()) {
    gpr_log(GPR_DEBUG, "%s: WakeInsideCombiner %s", LogTag().c_str(),
            DebugString().c_str());
  }
  if (send_initial_metadata_ != nullptr &&
      send_initial_metadata_->state ==
          SendInitialMetadata::kQueuedAndGotLatch) {
    send_initial_metadata_->state = SendInitialMetadata::kQueuedAndSetLatch;
    send_initial_metadata_->server_initial_metadata_publisher->Set(
        send_initial_metadata_->batch->payload->send_initial_metadata
            .send_initial_metadata);
  }
  poll_ctx.ClearRepoll();
  if (send_message() != nullptr) {
    send_message()->WakeInsideCombiner(flusher);
    if (send_trailing_state_ == SendTrailingState::kQueuedBehindSendMessage &&
        send_message()->IsIdle()) {
      send_trailing_state_ = SendTrailingState::kQueued;
    }
  }
  if (receive_message() != nullptr) {
    receive_message()->WakeInsideCombiner(flusher);
  }
  if (promise_.has_value()) {
    Poll<ServerMetadataHandle> poll;
    poll = promise_();
    if (grpc_trace_channel.enabled()) {
      gpr_log(GPR_DEBUG, "%s: WakeInsideCombiner poll=%s", LogTag().c_str(),
              PollToString(poll, [](const ServerMetadataHandle& h) {
                return h->DebugString();
              }).c_str());
    }
    if (send_initial_metadata_ != nullptr &&
        send_initial_metadata_->state ==
            SendInitialMetadata::kQueuedAndSetLatch) {
      Poll<ServerMetadata**> p = server_initial_metadata_latch()->Wait()();
      if (ServerMetadata*** ppp = absl::get_if<ServerMetadata**>(&p)) {
        ServerMetadata* md = **ppp;
        if (send_initial_metadata_->batch->payload->send_initial_metadata
                .send_initial_metadata != md) {
          *send_initial_metadata_->batch->payload->send_initial_metadata
               .send_initial_metadata = std::move(*md);
        }
        send_initial_metadata_->state = SendInitialMetadata::kForwarded;
        send_initial_metadata_->batch.ResumeWith(flusher);
      }
    }
    if (auto* r = absl::get_if<ServerMetadataHandle>(&poll)) {
      promise_ = ArenaPromise<ServerMetadataHandle>();
      auto* md = UnwrapMetadata(std::move(*r));
      bool destroy_md = true;
      if (send_message() != nullptr) {
        send_message()->Done(*md);
      }
      if (receive_message() != nullptr) {
        receive_message()->Done(*md, flusher);
      }
      switch (send_trailing_state_) {
        case SendTrailingState::kQueuedBehindSendMessage:
        case SendTrailingState::kQueued: {
          if (send_trailing_metadata_batch_->payload->send_trailing_metadata
                  .send_trailing_metadata != md) {
            *send_trailing_metadata_batch_->payload->send_trailing_metadata
                 .send_trailing_metadata = std::move(*md);
          } else {
            destroy_md = false;
          }
          send_trailing_metadata_batch_.ResumeWith(flusher);
          send_trailing_state_ = SendTrailingState::kForwarded;
        } break;
        case SendTrailingState::kForwarded:
          abort();  // unreachable
          break;
        case SendTrailingState::kInitial: {
          GPR_ASSERT(*md->get_pointer(GrpcStatusMetadata()) != GRPC_STATUS_OK);
          Completed(StatusFromMetadata(*md), flusher);
        } break;
        case SendTrailingState::kCancelled:
          // Nothing to do.
          break;
      }
      if (destroy_md) {
        md->~grpc_metadata_batch();
      }
    }
  }
}

void ServerCallData::OnWakeup() { abort(); }  // not implemented

}  // namespace promise_filter_detail
}  // namespace grpc_core
