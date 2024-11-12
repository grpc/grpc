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

#include "src/core/lib/channel/promise_based_filter.h"

#include <grpc/status.h>
#include <grpc/support/port_platform.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/crash.h"
#include "src/core/util/latent_see.h"
#include "src/core/util/manual_constructor.h"
#include "src/core/util/status_helper.h"

namespace grpc_core {
namespace promise_filter_detail {

namespace {
class FakeActivity final : public Activity {
 public:
  explicit FakeActivity(Activity* wake_activity)
      : wake_activity_(wake_activity) {}
  void Orphan() override {}
  void ForceImmediateRepoll(WakeupMask) override {}
  Waker MakeOwningWaker() override { return wake_activity_->MakeOwningWaker(); }
  Waker MakeNonOwningWaker() override {
    return wake_activity_->MakeNonOwningWaker();
  }
  void Run(absl::FunctionRef<void()> f) {
    ScopedActivity activity(this);
    f();
  }

 private:
  Activity* const wake_activity_;
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

BaseCallData::BaseCallData(
    grpc_call_element* elem, const grpc_call_element_args* args, uint8_t flags,
    absl::FunctionRef<Interceptor*()> make_send_interceptor,
    absl::FunctionRef<Interceptor*()> make_recv_interceptor)
    : call_stack_(args->call_stack),
      elem_(elem),
      arena_(args->arena),
      call_combiner_(args->call_combiner),
      deadline_(args->deadline),
      server_initial_metadata_pipe_(
          flags & kFilterExaminesServerInitialMetadata
              ? arena_->New<Pipe<ServerMetadataHandle>>(arena_)
              : nullptr),
      send_message_(
          flags & kFilterExaminesOutboundMessages
              ? arena_->New<SendMessage>(this, make_send_interceptor())
              : nullptr),
      receive_message_(
          flags & kFilterExaminesInboundMessages
              ? arena_->New<ReceiveMessage>(this, make_recv_interceptor())
              : nullptr) {}

BaseCallData::~BaseCallData() {
  FakeActivity(this).Run([this] {
    if (send_message_ != nullptr) {
      send_message_->~SendMessage();
    }
    if (receive_message_ != nullptr) {
      receive_message_->~ReceiveMessage();
    }
    if (server_initial_metadata_pipe_ != nullptr) {
      server_initial_metadata_pipe_->~Pipe();
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
  return Waker(this, 0);
}

void BaseCallData::Wakeup(WakeupMask) {
  auto wakeup = [](void* p, grpc_error_handle) {
    auto* self = static_cast<BaseCallData*>(p);
    self->OnWakeup();
    self->Drop(0);
  };
  auto* closure = GRPC_CLOSURE_CREATE(wakeup, this, nullptr);
  GRPC_CALL_COMBINER_START(call_combiner_, closure, absl::OkStatus(), "wakeup");
}

void BaseCallData::Drop(WakeupMask) {
  GRPC_CALL_STACK_UNREF(call_stack_, "waker");
}

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
  CHECK_NE(refcnt, 0u);
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
  CHECK_NE(batch, nullptr);
  uintptr_t& refcnt = *RefCountField(batch);
  if (refcnt == 0) {
    // refcnt==0 ==> cancelled
    GRPC_TRACE_LOG(channel, INFO)
        << releaser->call()->DebugTag() << "RESUME BATCH REQUEST CANCELLED";
    return;
  }
  if (--refcnt == 0) {
    releaser->Resume(batch);
  }
}

void BaseCallData::CapturedBatch::CompleteWith(Flusher* releaser) {
  auto* batch = std::exchange(batch_, nullptr);
  CHECK_NE(batch, nullptr);
  uintptr_t& refcnt = *RefCountField(batch);
  if (refcnt == 0) return;  // refcnt==0 ==> cancelled
  if (--refcnt == 0) {
    releaser->Complete(batch);
  }
}

void BaseCallData::CapturedBatch::CancelWith(grpc_error_handle error,
                                             Flusher* releaser) {
  auto* batch = std::exchange(batch_, nullptr);
  CHECK_NE(batch, nullptr);
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

BaseCallData::Flusher::Flusher(BaseCallData* call, latent_see::Metadata* desc)
    : latent_see::InnerScope(desc), call_(call) {
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
    GRPC_TRACE_LOG(channel, INFO)
        << "FLUSHER:forward batch via closure: "
        << grpc_transport_stream_op_batch_string(batch, false);
    grpc_call_next_op(call->elem(), batch);
    GRPC_CALL_STACK_UNREF(call->call_stack(), "flusher_batch");
  };
  for (size_t i = 1; i < release_.size(); i++) {
    auto* batch = release_[i];
    if (call_->call() != nullptr && call_->call()->traced()) {
      batch->is_traced = true;
    }
    GRPC_TRACE_LOG(channel, INFO)
        << "FLUSHER:queue batch to forward in closure: "
        << grpc_transport_stream_op_batch_string(release_[i], false);
    batch->handler_private.extra_arg = call_;
    GRPC_CLOSURE_INIT(&batch->handler_private.closure, call_next_op, batch,
                      nullptr);
    GRPC_CALL_STACK_REF(call_->call_stack(), "flusher_batch");
    call_closures_.Add(&batch->handler_private.closure, absl::OkStatus(),
                       "flusher_batch");
  }
  call_closures_.RunClosuresWithoutYielding(call_->call_combiner());
  GRPC_TRACE_LOG(channel, INFO)
      << "FLUSHER:forward batch: "
      << grpc_transport_stream_op_batch_string(release_[0], false);
  if (call_->call() != nullptr && call_->call()->traced()) {
    release_[0]->is_traced = true;
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
    case State::kCancelledButNotYetPolled:
      return "CANCELLED_BUT_NOT_YET_POLLED";
    case State::kCancelledButNoStatus:
      return "CANCELLED_BUT_NO_STATUS";
  }
  return "UNKNOWN";
}

void BaseCallData::SendMessage::StartOp(CapturedBatch batch) {
  GRPC_TRACE_LOG(channel, INFO)
      << base_->LogTag() << " SendMessage.StartOp st=" << StateString(state_);
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
      Crash(absl::StrFormat("ILLEGAL STATE: %s", StateString(state_)));
    case State::kCancelled:
    case State::kCancelledButNotYetPolled:
    case State::kCancelledButNoStatus:
      return;
  }
  batch_ = batch;
  intercepted_on_complete_ = std::exchange(batch_->on_complete, &on_complete_);
}

template <typename T>
void BaseCallData::SendMessage::GotPipe(T* pipe_end) {
  GRPC_TRACE_LOG(channel, INFO)
      << base_->LogTag() << " SendMessage.GotPipe st=" << StateString(state_);
  CHECK_NE(pipe_end, nullptr);
  switch (state_) {
    case State::kInitial:
      state_ = State::kIdle;
      GetContext<Activity>()->ForceImmediateRepoll();
      break;
    case State::kGotBatchNoPipe:
      state_ = State::kGotBatch;
      GetContext<Activity>()->ForceImmediateRepoll();
      break;
    case State::kIdle:
    case State::kGotBatch:
    case State::kForwardedBatch:
    case State::kBatchCompleted:
    case State::kPushedToPipe:
    case State::kCancelledButNoStatus:
      Crash(absl::StrFormat("ILLEGAL STATE: %s", StateString(state_)));
    case State::kCancelled:
    case State::kCancelledButNotYetPolled:
      return;
  }
  interceptor_->GotPipe(pipe_end);
}

bool BaseCallData::SendMessage::IsIdle() const {
  switch (state_) {
    case State::kInitial:
    case State::kIdle:
    case State::kForwardedBatch:
    case State::kCancelled:
    case State::kCancelledButNotYetPolled:
    case State::kCancelledButNoStatus:
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
  Flusher flusher(base_, GRPC_LATENT_SEE_METADATA("SendMessage::OnComplete"));
  GRPC_TRACE_LOG(channel, INFO)
      << base_->LogTag() << " SendMessage.OnComplete st=" << StateString(state_)
      << " status=" << status;
  switch (state_) {
    case State::kInitial:
    case State::kIdle:
    case State::kGotBatchNoPipe:
    case State::kPushedToPipe:
    case State::kGotBatch:
    case State::kBatchCompleted:
      Crash(absl::StrFormat("ILLEGAL STATE: %s", StateString(state_)));
      break;
    case State::kCancelled:
    case State::kCancelledButNotYetPolled:
    case State::kCancelledButNoStatus:
      flusher.AddClosure(intercepted_on_complete_, status,
                         "forward after cancel");
      break;
    case State::kForwardedBatch:
      completed_status_ = status;
      state_ = State::kBatchCompleted;
      ScopedContext ctx(base_);
      base_->WakeInsideCombiner(&flusher);
      break;
  }
}

void BaseCallData::SendMessage::Done(const ServerMetadata& metadata,
                                     Flusher* flusher) {
  GRPC_TRACE_LOG(channel, INFO)
      << base_->LogTag() << " SendMessage.Done st=" << StateString(state_)
      << " md=" << metadata.DebugString();
  switch (state_) {
    case State::kCancelled:
    case State::kCancelledButNotYetPolled:
      break;
    case State::kInitial:
      state_ = State::kCancelled;
      break;
    case State::kIdle:
    case State::kForwardedBatch:
      state_ = State::kCancelledButNotYetPolled;
      if (base_->is_current()) base_->ForceImmediateRepoll();
      break;
    case State::kCancelledButNoStatus:
    case State::kGotBatchNoPipe:
    case State::kGotBatch: {
      std::string temp;
      batch_.CancelWith(
          absl::Status(
              static_cast<absl::StatusCode>(metadata.get(GrpcStatusMetadata())
                                                .value_or(GRPC_STATUS_UNKNOWN)),
              metadata.GetStringValue("grpc-message", &temp).value_or("")),
          flusher);
      state_ = State::kCancelledButNotYetPolled;
    } break;
    case State::kBatchCompleted:
      Crash(absl::StrFormat("ILLEGAL STATE: %s", StateString(state_)));
      break;
    case State::kPushedToPipe:
      push_.reset();
      next_.reset();
      state_ = State::kCancelledButNotYetPolled;
      if (base_->is_current()) base_->ForceImmediateRepoll();
      break;
  }
}

void BaseCallData::SendMessage::WakeInsideCombiner(Flusher* flusher,
                                                   bool allow_push_to_pipe) {
  GRPC_TRACE_LOG(channel, INFO)
      << base_->LogTag()
      << " SendMessage.WakeInsideCombiner st=" << StateString(state_)
      << (state_ == State::kBatchCompleted
              ? absl::StrCat(" status=", completed_status_.ToString())
              : "");
  switch (state_) {
    case State::kInitial:
    case State::kIdle:
    case State::kGotBatchNoPipe:
    case State::kCancelled:
    case State::kCancelledButNoStatus:
      break;
    case State::kCancelledButNotYetPolled:
      interceptor()->Push()->Close();
      state_ = State::kCancelled;
      break;
    case State::kGotBatch:
      if (allow_push_to_pipe) {
        state_ = State::kPushedToPipe;
        auto message = Arena::MakePooled<Message>();
        message->payload()->Swap(batch_->payload->send_message.send_message);
        message->mutable_flags() = batch_->payload->send_message.flags;
        push_ = interceptor()->Push()->Push(std::move(message));
        next_.emplace(interceptor()->Pull()->Next());
      } else {
        break;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case State::kPushedToPipe: {
      CHECK(push_.has_value());
      auto r_push = (*push_)();
      if (auto* p = r_push.value_if_ready()) {
        GRPC_TRACE_LOG(channel, INFO)
            << base_->LogTag()
            << " SendMessage.WakeInsideCombiner push complete, "
               "result="
            << (*p ? "true" : "false");
        // We haven't pulled through yet, so this certainly shouldn't succeed.
        CHECK(!*p);
        state_ = State::kCancelled;
        batch_.CancelWith(absl::CancelledError(), flusher);
        break;
      }
      CHECK(next_.has_value());
      auto r_next = (*next_)();
      if (auto* p = r_next.value_if_ready()) {
        GRPC_TRACE_LOG(channel, INFO)
            << base_->LogTag()
            << " SendMessage.WakeInsideCombiner next complete, "
               "result.has_value="
            << (p->has_value() ? "true" : "false");
        if (p->has_value()) {
          batch_->payload->send_message.send_message->Swap((**p)->payload());
          batch_->payload->send_message.flags = (**p)->flags();
          state_ = State::kForwardedBatch;
          batch_.ResumeWith(flusher);
          next_.reset();
          if ((*push_)().ready()) push_.reset();
        } else {
          state_ = State::kCancelledButNoStatus;
          next_.reset();
          push_.reset();
        }
      }
    } break;
    case State::kForwardedBatch:
      if (push_.has_value() && (*push_)().ready()) {
        push_.reset();
      }
      break;
    case State::kBatchCompleted:
      if (push_.has_value() && (*push_)().pending()) {
        break;
      }
      if (completed_status_.ok()) {
        state_ = State::kIdle;
        GetContext<Activity>()->ForceImmediateRepoll();
      } else {
        state_ = State::kCancelled;
      }
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
    case State::kCancelledWhilstForwardingNoPipe:
      return "CANCELLED_WHILST_FORWARDING_NO_PIPE";
    case State::kBatchCompletedButCancelled:
      return "BATCH_COMPLETED_BUT_CANCELLED";
    case State::kBatchCompletedButCancelledNoPipe:
      return "BATCH_COMPLETED_BUT_CANCELLED_NO_PIPE";
    case State::kCancelledWhilstIdle:
      return "CANCELLED_WHILST_IDLE";
    case State::kCompletedWhilePulledFromPipe:
      return "COMPLETED_WHILE_PULLED_FROM_PIPE";
    case State::kCompletedWhilePushedToPipe:
      return "COMPLETED_WHILE_PUSHED_TO_PIPE";
    case State::kCompletedWhileBatchCompleted:
      return "COMPLETED_WHILE_BATCH_COMPLETED";
  }
  return "UNKNOWN";
}

void BaseCallData::ReceiveMessage::StartOp(CapturedBatch& batch) {
  GRPC_TRACE_LOG(channel, INFO)
      << base_->LogTag()
      << " ReceiveMessage.StartOp st=" << StateString(state_);
  switch (state_) {
    case State::kInitial:
      state_ = State::kForwardedBatchNoPipe;
      break;
    case State::kIdle:
      state_ = State::kForwardedBatch;
      break;
    case State::kCancelledWhilstForwarding:
    case State::kCancelledWhilstForwardingNoPipe:
    case State::kBatchCompletedButCancelled:
    case State::kBatchCompletedButCancelledNoPipe:
    case State::kForwardedBatch:
    case State::kForwardedBatchNoPipe:
    case State::kBatchCompleted:
    case State::kBatchCompletedNoPipe:
    case State::kCompletedWhileBatchCompleted:
    case State::kPushedToPipe:
    case State::kPulledFromPipe:
    case State::kCompletedWhilePulledFromPipe:
    case State::kCompletedWhilePushedToPipe:
      Crash(absl::StrFormat("ILLEGAL STATE: %s", StateString(state_)));
    case State::kCancelledWhilstIdle:
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

template <typename T>
void BaseCallData::ReceiveMessage::GotPipe(T* pipe_end) {
  GRPC_TRACE_LOG(channel, INFO)
      << base_->LogTag()
      << " ReceiveMessage.GotPipe st=" << StateString(state_);
  switch (state_) {
    case State::kInitial:
      state_ = State::kIdle;
      break;
    case State::kForwardedBatchNoPipe:
      state_ = State::kForwardedBatch;
      break;
    case State::kBatchCompletedNoPipe:
      state_ = State::kBatchCompleted;
      GetContext<Activity>()->ForceImmediateRepoll();
      break;
    case State::kIdle:
    case State::kForwardedBatch:
    case State::kBatchCompleted:
    case State::kPushedToPipe:
    case State::kPulledFromPipe:
    case State::kCompletedWhilePulledFromPipe:
    case State::kCompletedWhilePushedToPipe:
    case State::kCompletedWhileBatchCompleted:
    case State::kCancelledWhilstForwarding:
    case State::kCancelledWhilstForwardingNoPipe:
    case State::kCancelledWhilstIdle:
    case State::kBatchCompletedButCancelled:
    case State::kBatchCompletedButCancelledNoPipe:
      Crash(absl::StrFormat("ILLEGAL STATE: %s", StateString(state_)));
    case State::kCancelled:
      return;
  }
  interceptor()->GotPipe(pipe_end);
}

void BaseCallData::ReceiveMessage::OnComplete(absl::Status status) {
  GRPC_TRACE_LOG(channel, INFO)
      << base_->LogTag()
      << " ReceiveMessage.OnComplete st=" << StateString(state_)
      << " status=" << status;
  switch (state_) {
    case State::kInitial:
    case State::kIdle:
    case State::kPushedToPipe:
    case State::kPulledFromPipe:
    case State::kBatchCompleted:
    case State::kCompletedWhileBatchCompleted:
    case State::kBatchCompletedNoPipe:
    case State::kCancelled:
    case State::kBatchCompletedButCancelled:
    case State::kBatchCompletedButCancelledNoPipe:
    case State::kCancelledWhilstIdle:
    case State::kCompletedWhilePulledFromPipe:
    case State::kCompletedWhilePushedToPipe:
      Crash(absl::StrFormat("ILLEGAL STATE: %s", StateString(state_)));
    case State::kForwardedBatchNoPipe:
      state_ = State::kBatchCompletedNoPipe;
      break;
    case State::kForwardedBatch:
      state_ = State::kBatchCompleted;
      break;
    case State::kCancelledWhilstForwarding:
      state_ = State::kBatchCompletedButCancelled;
      break;
    case State::kCancelledWhilstForwardingNoPipe:
      state_ = State::kBatchCompletedButCancelledNoPipe;
      break;
  }
  completed_status_ = status;
  Flusher flusher(base_,
                  GRPC_LATENT_SEE_METADATA("ReceiveMessage::OnComplete"));
  ScopedContext ctx(base_);
  base_->WakeInsideCombiner(&flusher);
}

void BaseCallData::ReceiveMessage::Done(const ServerMetadata& metadata,
                                        Flusher* flusher) {
  GRPC_TRACE_LOG(channel, INFO)
      << base_->LogTag() << " ReceiveMessage.Done st=" << StateString(state_)
      << " md=" << metadata.DebugString();
  switch (state_) {
    case State::kInitial:
      state_ = State::kCancelled;
      break;
    case State::kIdle:
      state_ = State::kCancelledWhilstIdle;
      break;
    case State::kForwardedBatch:
      state_ = State::kCancelledWhilstForwarding;
      break;
    case State::kForwardedBatchNoPipe:
      state_ = State::kCancelledWhilstForwardingNoPipe;
      break;
    case State::kCompletedWhileBatchCompleted:
    case State::kBatchCompleted:
      state_ = State::kCompletedWhileBatchCompleted;
      break;
    case State::kCompletedWhilePulledFromPipe:
    case State::kCompletedWhilePushedToPipe:
    case State::kPulledFromPipe:
    case State::kPushedToPipe: {
      auto status_code =
          metadata.get(GrpcStatusMetadata()).value_or(GRPC_STATUS_UNKNOWN);
      if (status_code == GRPC_STATUS_OK) {
        if (state_ == State::kCompletedWhilePulledFromPipe ||
            state_ == State::kPulledFromPipe) {
          state_ = State::kCompletedWhilePulledFromPipe;
        } else {
          state_ = State::kCompletedWhilePushedToPipe;
        }
      } else {
        push_.reset();
        next_.reset();
        flusher->AddClosure(intercepted_on_complete_,
                            StatusFromMetadata(metadata), "recv_message_done");
        state_ = State::kCancelled;
      }
    } break;
    case State::kBatchCompletedNoPipe:
      state_ = State::kBatchCompletedButCancelledNoPipe;
      break;
    case State::kBatchCompletedButCancelled:
    case State::kBatchCompletedButCancelledNoPipe:
      Crash(absl::StrFormat("ILLEGAL STATE: %s", StateString(state_)));
    case State::kCancelledWhilstIdle:
    case State::kCancelledWhilstForwarding:
    case State::kCancelledWhilstForwardingNoPipe:
    case State::kCancelled:
      break;
  }
}

void BaseCallData::ReceiveMessage::WakeInsideCombiner(Flusher* flusher,
                                                      bool allow_push_to_pipe) {
  GRPC_TRACE_LOG(channel, INFO)
      << base_->LogTag()
      << " ReceiveMessage.WakeInsideCombiner st=" << StateString(state_)
      << " push?=" << (push_.has_value() ? "yes" : "no")
      << " next?=" << (next_.has_value() ? "yes" : "no")
      << " allow_push_to_pipe=" << (allow_push_to_pipe ? "yes" : "no");
  switch (state_) {
    case State::kInitial:
    case State::kIdle:
    case State::kForwardedBatchNoPipe:
    case State::kForwardedBatch:
    case State::kCancelled:
    case State::kCancelledWhilstForwarding:
    case State::kCancelledWhilstForwardingNoPipe:
    case State::kBatchCompletedNoPipe:
      break;
    case State::kCancelledWhilstIdle:
      interceptor()->Push()->Close();
      state_ = State::kCancelled;
      break;
    case State::kBatchCompletedButCancelled:
    case State::kCompletedWhileBatchCompleted:
      interceptor()->Push()->Close();
      state_ = State::kCancelled;
      flusher->AddClosure(std::exchange(intercepted_on_complete_, nullptr),
                          completed_status_, "recv_message");
      break;
    case State::kBatchCompletedButCancelledNoPipe:
      state_ = State::kCancelled;
      flusher->AddClosure(std::exchange(intercepted_on_complete_, nullptr),
                          completed_status_, "recv_message");
      break;
    case State::kBatchCompleted:
      if (completed_status_.ok() && intercepted_slice_buffer_->has_value()) {
        if (!allow_push_to_pipe) break;
        if (state_ == State::kBatchCompleted) {
          state_ = State::kPushedToPipe;
        } else {
          state_ = State::kCompletedWhilePushedToPipe;
        }
        auto message = Arena::MakePooled<Message>();
        message->payload()->Swap(&**intercepted_slice_buffer_);
        message->mutable_flags() = *intercepted_flags_;
        push_ = interceptor()->Push()->Push(std::move(message));
        next_.emplace(interceptor()->Pull()->Next());
      } else {
        interceptor()->Push()->Close();
        state_ = State::kCancelled;
        flusher->AddClosure(std::exchange(intercepted_on_complete_, nullptr),
                            completed_status_, "recv_message");
        break;
      }
      CHECK(state_ == State::kPushedToPipe ||
            state_ == State::kCompletedWhilePushedToPipe);
      ABSL_FALLTHROUGH_INTENDED;
    case State::kCompletedWhilePushedToPipe:
    case State::kPushedToPipe: {
      CHECK(push_.has_value());
      auto r_push = (*push_)();
      if (auto* p = r_push.value_if_ready()) {
        GRPC_TRACE_LOG(channel, INFO)
            << base_->LogTag()
            << " ReceiveMessage.WakeInsideCombiner push complete: "
            << (*p ? "true" : "false");
        // We haven't pulled through yet, so this certainly shouldn't succeed.
        CHECK(!*p);
        state_ = State::kCancelled;
        break;
      }
      CHECK(next_.has_value());
      auto r_next = (*next_)();
      if (auto* p = r_next.value_if_ready()) {
        next_.reset();
        if (p->has_value()) {
          *intercepted_slice_buffer_ = std::move(*(**p)->payload());
          *intercepted_flags_ = (**p)->flags();
          if (state_ == State::kCompletedWhilePushedToPipe) {
            state_ = State::kCompletedWhilePulledFromPipe;
          } else {
            state_ = State::kPulledFromPipe;
          }
        } else {
          *intercepted_slice_buffer_ = absl::nullopt;
          *intercepted_flags_ = 0;
          state_ = State::kCancelled;
          flusher->AddClosure(
              std::exchange(intercepted_on_complete_, nullptr),
              p->cancelled() ? absl::CancelledError() : absl::OkStatus(),
              "recv_message");
        }
        GRPC_TRACE_LOG(channel, INFO)
            << base_->LogTag()
            << " ReceiveMessage.WakeInsideCombiner next complete: "
            << (p->has_value() ? "got message" : "end of stream")
            << " new_state=" << StateString(state_);
      }
      if (state_ != State::kPulledFromPipe &&
          state_ != State::kCompletedWhilePulledFromPipe) {
        break;
      }
    }
      ABSL_FALLTHROUGH_INTENDED;
    case State::kCompletedWhilePulledFromPipe:
    case State::kPulledFromPipe: {
      CHECK(push_.has_value());
      if ((*push_)().ready()) {
        GRPC_TRACE_LOG(channel, INFO)
            << base_->LogTag()
            << " ReceiveMessage.WakeInsideCombiner push complete";
        if (state_ == State::kCompletedWhilePulledFromPipe) {
          interceptor()->Push()->Close();
          state_ = State::kCancelled;
        } else {
          state_ = State::kIdle;
        }
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
    kGotPipe,
    // Responded to trailing metadata prior to getting a recv_initial_metadata
    kRespondedToTrailingMetadataPriorToHook,
    // Hooked, no latch yet
    kHookedWaitingForPipe,
    // Hooked, latch seen
    kHookedAndGotPipe,
    // Got the callback, haven't set latch yet
    kCompleteWaitingForPipe,
    // Got the callback and got the latch
    kCompleteAndGotPipe,
    // Got the callback and set the latch
    kCompleteAndPushedToPipe,
    // Called the original callback
    kResponded,
    // Called the original callback with an error: still need to set the latch
    kRespondedButNeedToClosePipe,
  };

  State state = kInitial;
  grpc_closure* original_on_ready = nullptr;
  grpc_closure on_ready;
  grpc_metadata_batch* metadata = nullptr;
  PipeSender<ServerMetadataHandle>* server_initial_metadata_publisher = nullptr;
  absl::optional<PipeSender<ServerMetadataHandle>::PushType> metadata_push_;
  absl::optional<PipeReceiverNextType<ServerMetadataHandle>> metadata_next_;

  static const char* StateString(State state) {
    switch (state) {
      case kInitial:
        return "INITIAL";
      case kGotPipe:
        return "GOT_PIPE";
      case kRespondedToTrailingMetadataPriorToHook:
        return "RESPONDED_TO_TRAILING_METADATA_PRIOR_TO_HOOK";
      case kHookedWaitingForPipe:
        return "HOOKED_WAITING_FOR_PIPE";
      case kHookedAndGotPipe:
        return "HOOKED_AND_GOT_PIPE";
      case kCompleteWaitingForPipe:
        return "COMPLETE_WAITING_FOR_PIPE";
      case kCompleteAndGotPipe:
        return "COMPLETE_AND_GOT_PIPE";
      case kCompleteAndPushedToPipe:
        return "COMPLETE_AND_PUSHED_TO_PIPE";
      case kResponded:
        return "RESPONDED";
      case kRespondedButNeedToClosePipe:
        return "RESPONDED_BUT_NEED_TO_CLOSE_PIPE";
    }
    return "UNKNOWN";
  }

  bool AllowRecvMessage() const {
    switch (state) {
      case kInitial:
      case kGotPipe:
      case kHookedWaitingForPipe:
      case kHookedAndGotPipe:
      case kCompleteWaitingForPipe:
      case kCompleteAndGotPipe:
      case kCompleteAndPushedToPipe:
      case kRespondedToTrailingMetadataPriorToHook:
        return false;
      case kResponded:
      case kRespondedButNeedToClosePipe:
        return true;
    }
    GPR_UNREACHABLE_CODE(return false);
  }
};

class ClientCallData::PollContext {
 public:
  explicit PollContext(ClientCallData* self, Flusher* flusher)
      : self_(self), flusher_(flusher) {
    CHECK_EQ(self_->poll_ctx_, nullptr);

    self_->poll_ctx_ = this;
    scoped_activity_.Init(self_);
    have_scoped_activity_ = true;
  }

  PollContext(const PollContext&) = delete;
  PollContext& operator=(const PollContext&) = delete;

  void Run() {
    DCHECK(HasContext<Arena>());
    GRPC_TRACE_LOG(channel, INFO)
        << self_->LogTag() << " ClientCallData.PollContext.Run "
        << self_->DebugString();
    CHECK(have_scoped_activity_);
    repoll_ = false;
    if (self_->send_message() != nullptr) {
      self_->send_message()->WakeInsideCombiner(flusher_, true);
    }
    if (self_->receive_message() != nullptr) {
      self_->receive_message()->WakeInsideCombiner(
          flusher_, self_->recv_initial_metadata_ == nullptr
                        ? true
                        : self_->recv_initial_metadata_->AllowRecvMessage());
    }
    if (self_->server_initial_metadata_pipe() != nullptr) {
      if (self_->recv_initial_metadata_->metadata_push_.has_value()) {
        if ((*self_->recv_initial_metadata_->metadata_push_)().ready()) {
          self_->recv_initial_metadata_->metadata_push_.reset();
        }
      }
      switch (self_->recv_initial_metadata_->state) {
        case RecvInitialMetadata::kInitial:
        case RecvInitialMetadata::kGotPipe:
        case RecvInitialMetadata::kHookedWaitingForPipe:
        case RecvInitialMetadata::kHookedAndGotPipe:
        case RecvInitialMetadata::kCompleteWaitingForPipe:
        case RecvInitialMetadata::kResponded:
        case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
          break;
        case RecvInitialMetadata::kRespondedButNeedToClosePipe:
          self_->recv_initial_metadata_->server_initial_metadata_publisher
              ->Close();
          self_->recv_initial_metadata_->state =
              RecvInitialMetadata::kResponded;
          break;
        case RecvInitialMetadata::kCompleteAndGotPipe:
          self_->recv_initial_metadata_->state =
              RecvInitialMetadata::kCompleteAndPushedToPipe;
          CHECK(!self_->recv_initial_metadata_->metadata_push_.has_value());
          CHECK(!self_->recv_initial_metadata_->metadata_next_.has_value());
          self_->recv_initial_metadata_->metadata_push_.emplace(
              self_->recv_initial_metadata_->server_initial_metadata_publisher
                  ->Push(ServerMetadataHandle(
                      self_->recv_initial_metadata_->metadata,
                      Arena::PooledDeleter(nullptr))));
          repoll_ = true;  // ensure Push() gets polled.
          self_->recv_initial_metadata_->metadata_next_.emplace(
              self_->server_initial_metadata_pipe()->receiver.Next());
          ABSL_FALLTHROUGH_INTENDED;
        case RecvInitialMetadata::kCompleteAndPushedToPipe: {
          CHECK(self_->recv_initial_metadata_->metadata_next_.has_value());
          Poll<NextResult<ServerMetadataHandle>> p =
              (*self_->recv_initial_metadata_->metadata_next_)();
          if (NextResult<ServerMetadataHandle>* nr = p.value_if_ready()) {
            if (nr->has_value()) {
              ServerMetadataHandle md = std::move(nr->value());
              if (self_->recv_initial_metadata_->metadata != md.get()) {
                *self_->recv_initial_metadata_->metadata = std::move(*md);
              }
            } else {
              self_->recv_initial_metadata_->metadata->Clear();
            }
            self_->recv_initial_metadata_->state =
                RecvInitialMetadata::kResponded;
            repoll_ = true;
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
        GRPC_TRACE_LOG(channel, INFO)
            << self_->LogTag() << " ClientCallData.PollContext.Run: poll="
            << PollToString(poll,
                            [](const ServerMetadataHandle& h) {
                              return h->DebugString();
                            })
            << "; " << self_->DebugString();

        if (auto* r = poll.value_if_ready()) {
          auto md = std::move(*r);
          if (self_->send_message() != nullptr) {
            self_->send_message()->Done(*md, flusher_);
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
                case RecvInitialMetadata::kGotPipe:
                  self_->recv_initial_metadata_->state = RecvInitialMetadata::
                      kRespondedToTrailingMetadataPriorToHook;
                  break;
                case RecvInitialMetadata::
                    kRespondedToTrailingMetadataPriorToHook:
                case RecvInitialMetadata::kRespondedButNeedToClosePipe:
                  Crash(absl::StrFormat("ILLEGAL STATE: %s",
                                        RecvInitialMetadata::StateString(
                                            self_->recv_initial_metadata_
                                                ->state)));  // not reachable
                  break;
                case RecvInitialMetadata::kHookedWaitingForPipe:
                case RecvInitialMetadata::kHookedAndGotPipe:
                case RecvInitialMetadata::kResponded:
                case RecvInitialMetadata::kCompleteAndGotPipe:
                case RecvInitialMetadata::kCompleteAndPushedToPipe:
                  break;
                case RecvInitialMetadata::kCompleteWaitingForPipe:
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
            CHECK(!self_->cancelled_error_.ok());
            if (self_->recv_initial_metadata_ != nullptr) {
              switch (self_->recv_initial_metadata_->state) {
                case RecvInitialMetadata::kInitial:
                case RecvInitialMetadata::kGotPipe:
                  self_->recv_initial_metadata_->state = RecvInitialMetadata::
                      kRespondedToTrailingMetadataPriorToHook;
                  break;
                case RecvInitialMetadata::kHookedWaitingForPipe:
                case RecvInitialMetadata::kHookedAndGotPipe:
                case RecvInitialMetadata::kResponded:
                  break;
                case RecvInitialMetadata::
                    kRespondedToTrailingMetadataPriorToHook:
                case RecvInitialMetadata::kRespondedButNeedToClosePipe:
                  Crash(absl::StrFormat("ILLEGAL STATE: %s",
                                        RecvInitialMetadata::StateString(
                                            self_->recv_initial_metadata_
                                                ->state)));  // not reachable
                  break;
                case RecvInitialMetadata::kCompleteWaitingForPipe:
                case RecvInitialMetadata::kCompleteAndGotPipe:
                case RecvInitialMetadata::kCompleteAndPushedToPipe:
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
              CHECK(
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
          ScopedContext ctx(next_poll->call_data);
          Flusher flusher(next_poll->call_data,
                          GRPC_LATENT_SEE_METADATA(
                              "ClientCallData::PollContext::~PollContext"));
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
    : BaseCallData(
          elem, args, flags,
          [args]() {
            return args->arena->New<ReceiveInterceptor>(args->arena);
          },
          [args]() { return args->arena->New<SendInterceptor>(args->arena); }),
      initial_metadata_outstanding_token_(
          (flags & kFilterIsLast) != 0
              ? ClientInitialMetadataOutstandingToken::New(arena())
              : ClientInitialMetadataOutstandingToken::Empty()) {
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                    RecvTrailingMetadataReadyCallback, this,
                    grpc_schedule_on_exec_ctx);
  if (server_initial_metadata_pipe() != nullptr) {
    recv_initial_metadata_ = arena()->New<RecvInitialMetadata>();
  }
}

ClientCallData::~ClientCallData() {
  ScopedActivity scoped_activity(this);
  CHECK_EQ(poll_ctx_, nullptr);
  if (recv_initial_metadata_ != nullptr) {
    recv_initial_metadata_->~RecvInitialMetadata();
  }
  initial_metadata_outstanding_token_ =
      ClientInitialMetadataOutstandingToken::Empty();
}

std::string ClientCallData::DebugTag() const {
  return absl::StrFormat("PBF_CLIENT[%p]: [%v] ", this, elem()->filter->name);
}

// Activity implementation.
void ClientCallData::ForceImmediateRepoll(WakeupMask) {
  CHECK_NE(poll_ctx_, nullptr);
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
      server_initial_metadata_pipe() == nullptr
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
  Flusher flusher(this, GRPC_LATENT_SEE_METADATA("ClientCallData::StartBatch"));

  GRPC_TRACE_LOG(channel, INFO) << LogTag() << " StartBatch " << DebugString();

  // If this is a cancel stream, cancel anything we have pending and propagate
  // the cancellation.
  if (batch->cancel_stream) {
    CHECK(!batch->send_initial_metadata && !batch->send_trailing_metadata &&
          !batch->send_message && !batch->recv_initial_metadata &&
          !batch->recv_message && !batch->recv_trailing_metadata);
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
            RecvInitialMetadata::kHookedWaitingForPipe;
        break;
      case RecvInitialMetadata::kGotPipe:
        recv_initial_metadata_->state = RecvInitialMetadata::kHookedAndGotPipe;
        break;
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
        hook = false;
        break;
      case RecvInitialMetadata::kHookedWaitingForPipe:
      case RecvInitialMetadata::kHookedAndGotPipe:
      case RecvInitialMetadata::kCompleteWaitingForPipe:
      case RecvInitialMetadata::kCompleteAndGotPipe:
      case RecvInitialMetadata::kCompleteAndPushedToPipe:
      case RecvInitialMetadata::kResponded:
      case RecvInitialMetadata::kRespondedButNeedToClosePipe:
        Crash(absl::StrFormat(
            "ILLEGAL STATE: %s",
            RecvInitialMetadata::StateString(
                recv_initial_metadata_->state)));  // unreachable
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
      CHECK(send_initial_state_ == SendInitialState::kInitial);
      // Mark ourselves as queued.
      send_initial_state_ = SendInitialState::kQueued;
      if (batch->recv_trailing_metadata) {
        // If there's a recv_trailing_metadata op, we queue that too.
        CHECK(recv_trailing_state_ == RecvTrailingState::kInitial);
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
      CHECK(recv_trailing_state_ == RecvTrailingState::kInitial);
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
  GRPC_TRACE_LOG(channel, INFO)
      << LogTag() << " Cancel error=" << error.ToString();
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
      case RecvInitialMetadata::kCompleteWaitingForPipe:
      case RecvInitialMetadata::kCompleteAndGotPipe:
      case RecvInitialMetadata::kCompleteAndPushedToPipe:
        recv_initial_metadata_->state = RecvInitialMetadata::kResponded;
        GRPC_CALL_COMBINER_START(
            call_combiner(),
            std::exchange(recv_initial_metadata_->original_on_ready, nullptr),
            error, "propagate cancellation");
        break;
      case RecvInitialMetadata::kInitial:
      case RecvInitialMetadata::kGotPipe:
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
      case RecvInitialMetadata::kHookedWaitingForPipe:
      case RecvInitialMetadata::kHookedAndGotPipe:
      case RecvInitialMetadata::kResponded:
        break;
      case RecvInitialMetadata::kRespondedButNeedToClosePipe:
        Crash(absl::StrFormat(
            "ILLEGAL STATE: %s",
            RecvInitialMetadata::StateString(recv_initial_metadata_->state)));
        break;
    }
  }
  if (send_message() != nullptr) {
    send_message()->Done(*ServerMetadataFromStatus(error), flusher);
  }
  if (receive_message() != nullptr) {
    receive_message()->Done(*ServerMetadataFromStatus(error), flusher);
  }
}

// Begin running the promise - which will ultimately take some initial
// metadata and return some trailing metadata.
void ClientCallData::StartPromise(Flusher* flusher) {
  CHECK(send_initial_state_ == SendInitialState::kQueued);
  ChannelFilter* filter = promise_filter_detail::ChannelFilterFromElem(elem());

  // Construct the promise.
  PollContext ctx(this, flusher);
  promise_ = filter->MakeCallPromise(
      CallArgs{WrapMetadata(send_initial_metadata_batch_->payload
                                ->send_initial_metadata.send_initial_metadata),
               std::move(initial_metadata_outstanding_token_), nullptr,
               server_initial_metadata_pipe() == nullptr
                   ? nullptr
                   : &server_initial_metadata_pipe()->sender,
               send_message() == nullptr
                   ? nullptr
                   : send_message()->interceptor()->original_receiver(),
               receive_message() == nullptr
                   ? nullptr
                   : receive_message()->interceptor()->original_sender()},
      [this](CallArgs call_args) {
        return MakeNextPromise(std::move(call_args));
      });
  ctx.Run();
}

void ClientCallData::RecvInitialMetadataReady(grpc_error_handle error) {
  GRPC_TRACE_LOG(channel, INFO)
      << LogTag() << " ClientCallData.RecvInitialMetadataReady "
      << DebugString() << " error:" << error.ToString()
      << " md:" << recv_initial_metadata_->metadata->DebugString();
  ScopedContext context(this);
  Flusher flusher(this, GRPC_LATENT_SEE_METADATA(
                            "ClientCallData::RecvInitialMetadataReady"));
  if (!error.ok()) {
    switch (recv_initial_metadata_->state) {
      case RecvInitialMetadata::kHookedWaitingForPipe:
        recv_initial_metadata_->state = RecvInitialMetadata::kResponded;
        break;
      case RecvInitialMetadata::kHookedAndGotPipe:
        recv_initial_metadata_->state =
            RecvInitialMetadata::kRespondedButNeedToClosePipe;
        break;
      case RecvInitialMetadata::kInitial:
      case RecvInitialMetadata::kGotPipe:
      case RecvInitialMetadata::kCompleteWaitingForPipe:
      case RecvInitialMetadata::kCompleteAndGotPipe:
      case RecvInitialMetadata::kCompleteAndPushedToPipe:
      case RecvInitialMetadata::kResponded:
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
      case RecvInitialMetadata::kRespondedButNeedToClosePipe:
        Crash(absl::StrFormat(
            "ILLEGAL STATE: %s",
            RecvInitialMetadata::StateString(
                recv_initial_metadata_->state)));  // unreachable
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
      case RecvInitialMetadata::kHookedWaitingForPipe:
        recv_initial_metadata_->state =
            RecvInitialMetadata::kCompleteWaitingForPipe;
        break;
      case RecvInitialMetadata::kHookedAndGotPipe:
        recv_initial_metadata_->state =
            RecvInitialMetadata::kCompleteAndGotPipe;
        break;
      case RecvInitialMetadata::kInitial:
      case RecvInitialMetadata::kGotPipe:
      case RecvInitialMetadata::kCompleteWaitingForPipe:
      case RecvInitialMetadata::kCompleteAndGotPipe:
      case RecvInitialMetadata::kCompleteAndPushedToPipe:
      case RecvInitialMetadata::kResponded:
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
      case RecvInitialMetadata::kRespondedButNeedToClosePipe:
        Crash(absl::StrFormat(
            "ILLEGAL STATE: %s",
            RecvInitialMetadata::StateString(
                recv_initial_metadata_->state)));  // unreachable
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
  GRPC_TRACE_LOG(channel, INFO)
      << LogTag() << " ClientCallData.MakeNextPromise " << DebugString();
  CHECK_NE(poll_ctx_, nullptr);
  CHECK(send_initial_state_ == SendInitialState::kQueued);
  send_initial_metadata_batch_->payload->send_initial_metadata
      .send_initial_metadata = call_args.client_initial_metadata.get();
  if (recv_initial_metadata_ != nullptr) {
    // Call args should contain a latch for receiving initial metadata.
    // It might be the one we passed in - in which case we know this filter
    // only wants to examine the metadata, or it might be a new instance, in
    // which case we know the filter wants to mutate.
    CHECK_NE(call_args.server_initial_metadata, nullptr);
    recv_initial_metadata_->server_initial_metadata_publisher =
        call_args.server_initial_metadata;
    switch (recv_initial_metadata_->state) {
      case RecvInitialMetadata::kInitial:
        recv_initial_metadata_->state = RecvInitialMetadata::kGotPipe;
        break;
      case RecvInitialMetadata::kHookedWaitingForPipe:
        recv_initial_metadata_->state = RecvInitialMetadata::kHookedAndGotPipe;
        poll_ctx_->Repoll();
        break;
      case RecvInitialMetadata::kCompleteWaitingForPipe:
        recv_initial_metadata_->state =
            RecvInitialMetadata::kCompleteAndGotPipe;
        poll_ctx_->Repoll();
        break;
      case RecvInitialMetadata::kGotPipe:
      case RecvInitialMetadata::kHookedAndGotPipe:
      case RecvInitialMetadata::kCompleteAndGotPipe:
      case RecvInitialMetadata::kCompleteAndPushedToPipe:
      case RecvInitialMetadata::kResponded:
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
      case RecvInitialMetadata::kRespondedButNeedToClosePipe:
        Crash(absl::StrFormat(
            "ILLEGAL STATE: %s",
            RecvInitialMetadata::StateString(
                recv_initial_metadata_->state)));  // unreachable
    }
  } else {
    CHECK_EQ(call_args.server_initial_metadata, nullptr);
  }
  if (send_message() != nullptr) {
    send_message()->GotPipe(call_args.client_to_server_messages);
  } else {
    CHECK_EQ(call_args.client_to_server_messages, nullptr);
  }
  if (receive_message() != nullptr) {
    receive_message()->GotPipe(call_args.server_to_client_messages);
  } else {
    CHECK_EQ(call_args.server_to_client_messages, nullptr);
  }
  return ArenaPromise<ServerMetadataHandle>(
      [this]() { return PollTrailingMetadata(); });
}

// Wrapper to make it look like we're calling the next filter as a promise.
// First poll: send the send_initial_metadata op down the stack.
// All polls: await receiving the trailing metadata, then return it to the
// application.
Poll<ServerMetadataHandle> ClientCallData::PollTrailingMetadata() {
  GRPC_TRACE_LOG(channel, INFO)
      << LogTag() << " ClientCallData.PollTrailingMetadata " << DebugString();
  CHECK_NE(poll_ctx_, nullptr);
  if (send_initial_state_ == SendInitialState::kQueued) {
    // First poll: pass the send_initial_metadata op down the stack.
    CHECK(send_initial_metadata_batch_.is_captured());
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
      Crash(absl::StrFormat("ILLEGAL STATE: %s",
                            StateString(recv_trailing_state_)));
  }
  GPR_UNREACHABLE_CODE(return Pending{});
}

void ClientCallData::RecvTrailingMetadataReadyCallback(
    void* arg, grpc_error_handle error) {
  static_cast<ClientCallData*>(arg)->RecvTrailingMetadataReady(error);
}

void ClientCallData::RecvTrailingMetadataReady(grpc_error_handle error) {
  Flusher flusher(this, GRPC_LATENT_SEE_METADATA(
                            "ClientCallData::RecvTrailingMetadataReady"));
  GRPC_TRACE_LOG(channel, INFO)
      << LogTag() << " ClientCallData.RecvTrailingMetadataReady "
      << "recv_trailing_state=" << StateString(recv_trailing_state_)
      << " error=" << error << " md=" << recv_trailing_metadata_->DebugString();
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
  CHECK(recv_trailing_state_ == RecvTrailingState::kForwarded);
  recv_trailing_state_ = RecvTrailingState::kComplete;
  if (receive_message() != nullptr) {
    receive_message()->Done(*recv_trailing_metadata_, &flusher);
  }
  if (send_message() != nullptr) {
    send_message()->Done(*recv_trailing_metadata_, &flusher);
  }
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
  GRPC_LATENT_SEE_INNER_SCOPE("ClientCallData::WakeInsideCombiner");
  PollContext(this, flusher).Run();
}

void ClientCallData::OnWakeup() {
  Flusher flusher(this, GRPC_LATENT_SEE_METADATA("ClientCallData::OnWakeup"));
  ScopedContext context(this);
  WakeInsideCombiner(&flusher);
}

///////////////////////////////////////////////////////////////////////////////
// ServerCallData

struct ServerCallData::SendInitialMetadata {
  enum State {
    kInitial,
    kGotPipe,
    kQueuedWaitingForPipe,
    kQueuedAndGotPipe,
    kQueuedAndPushedToPipe,
    kForwarded,
    kCancelled,
  };
  State state = kInitial;
  CapturedBatch batch;
  PipeSender<ServerMetadataHandle>* server_initial_metadata_publisher = nullptr;
  absl::optional<PipeSender<ServerMetadataHandle>::PushType> metadata_push_;
  absl::optional<PipeReceiverNextType<ServerMetadataHandle>> metadata_next_;

  static const char* StateString(State state) {
    switch (state) {
      case kInitial:
        return "INITIAL";
      case kGotPipe:
        return "GOT_PIPE";
      case kQueuedWaitingForPipe:
        return "QUEUED_WAITING_FOR_PIPE";
      case kQueuedAndGotPipe:
        return "QUEUED_AND_GOT_PIPE";
      case kQueuedAndPushedToPipe:
        return "QUEUED_AND_PUSHED_TO_PIPE";
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
  explicit PollContext(ServerCallData* self, Flusher* flusher,
                       DebugLocation created = DebugLocation())
      : self_(self), flusher_(flusher), created_(created) {
    if (self_->poll_ctx_ != nullptr) {
      Crash(absl::StrCat(
          "PollContext: disallowed recursion. New: ", created_.file(), ":",
          created_.line(), "; Old: ", self_->poll_ctx_->created_.file(), ":",
          self_->poll_ctx_->created_.line()));
    }
    CHECK_EQ(self_->poll_ctx_, nullptr);
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
          Flusher flusher(next_poll->call_data,
                          GRPC_LATENT_SEE_METADATA(
                              "ServerCallData::PollContext::~PollContext"));
          ScopedContext context(next_poll->call_data);
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
  GPR_NO_UNIQUE_ADDRESS DebugLocation created_;
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
    case SendTrailingState::kQueuedButHaventClosedSends:
      return "QUEUED_BUT_HAVENT_CLOSED_SENDS";
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
    : BaseCallData(
          elem, args, flags,
          [args]() { return args->arena->New<SendInterceptor>(args->arena); },
          [args]() {
            return args->arena->New<ReceiveInterceptor>(args->arena);
          }) {
  if (server_initial_metadata_pipe() != nullptr) {
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
  GRPC_TRACE_LOG(channel, INFO)
      << LogTag() << " ~ServerCallData " << DebugString();
  if (send_initial_metadata_ != nullptr) {
    send_initial_metadata_->~SendInitialMetadata();
  }
  CHECK_EQ(poll_ctx_, nullptr);
}

std::string ServerCallData::DebugTag() const {
  return absl::StrFormat("PBF_SERVER[%p]: [%v] ", this, elem()->filter->name);
}

// Activity implementation.
void ServerCallData::ForceImmediateRepoll(WakeupMask) {
  CHECK_NE(poll_ctx_, nullptr);
  poll_ctx_->Repoll();
}

// Handle one grpc_transport_stream_op_batch
void ServerCallData::StartBatch(grpc_transport_stream_op_batch* b) {
  // Fake out the activity based context.
  ScopedContext context(this);
  CapturedBatch batch(b);
  Flusher flusher(this, GRPC_LATENT_SEE_METADATA("ServerCallData::StartBatch"));
  bool wake = false;

  GRPC_TRACE_LOG(channel, INFO) << LogTag() << " StartBatch: " << DebugString();

  // If this is a cancel stream, cancel anything we have pending and
  // propagate the cancellation.
  if (batch->cancel_stream) {
    CHECK(!batch->send_initial_metadata && !batch->send_trailing_metadata &&
          !batch->send_message && !batch->recv_initial_metadata &&
          !batch->recv_message && !batch->recv_trailing_metadata);
    PollContext poll_ctx(this, &flusher);
    Completed(batch->payload->cancel_stream.cancel_error,
              batch->payload->cancel_stream.tarpit, &flusher);
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
    CHECK(!batch->send_initial_metadata && !batch->send_trailing_metadata &&
          !batch->send_message && !batch->recv_message &&
          !batch->recv_trailing_metadata);
    // Otherwise, we should not have seen a send_initial_metadata op yet.
    CHECK(recv_initial_state_ == RecvInitialState::kInitial);
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
            SendInitialMetadata::kQueuedWaitingForPipe;
        break;
      case SendInitialMetadata::kGotPipe:
        send_initial_metadata_->state = SendInitialMetadata::kQueuedAndGotPipe;
        break;
      case SendInitialMetadata::kCancelled:
        batch.CancelWith(
            cancelled_error_.ok() ? absl::CancelledError() : cancelled_error_,
            &flusher);
        break;
      case SendInitialMetadata::kQueuedAndGotPipe:
      case SendInitialMetadata::kQueuedWaitingForPipe:
      case SendInitialMetadata::kQueuedAndPushedToPipe:
      case SendInitialMetadata::kForwarded:
        Crash(absl::StrFormat(
            "ILLEGAL STATE: %s",
            SendInitialMetadata::StateString(
                send_initial_metadata_->state)));  // not reachable
    }
    send_initial_metadata_->batch = batch;
    wake = true;
  }

  if (send_message() != nullptr && batch.is_captured() && batch->send_message) {
    send_message()->StartOp(batch);
    wake = true;
  }
  if (receive_message() != nullptr && batch.is_captured() &&
      batch->recv_message) {
    receive_message()->StartOp(batch);
    wake = true;
  }

  // send_trailing_metadata
  if (batch.is_captured() && batch->send_trailing_metadata) {
    switch (send_trailing_state_) {
      case SendTrailingState::kInitial:
        send_trailing_metadata_batch_ = batch;
        if (receive_message() != nullptr &&
            batch->payload->send_trailing_metadata.send_trailing_metadata
                    ->get(GrpcStatusMetadata())
                    .value_or(GRPC_STATUS_UNKNOWN) != GRPC_STATUS_OK) {
          receive_message()->Done(
              *batch->payload->send_trailing_metadata.send_trailing_metadata,
              &flusher);
        }
        if (send_message() != nullptr && !send_message()->IsIdle()) {
          send_trailing_state_ = SendTrailingState::kQueuedBehindSendMessage;
        } else if (send_message() != nullptr) {
          send_trailing_state_ = SendTrailingState::kQueuedButHaventClosedSends;
          wake = true;
        } else {
          send_trailing_state_ = SendTrailingState::kQueued;
          wake = true;
        }
        break;
      case SendTrailingState::kQueued:
      case SendTrailingState::kQueuedBehindSendMessage:
      case SendTrailingState::kQueuedButHaventClosedSends:
      case SendTrailingState::kForwarded:
        Crash(
            absl::StrFormat("ILLEGAL STATE: %s",
                            StateString(send_trailing_state_)));  // unreachable
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
void ServerCallData::Completed(grpc_error_handle error,
                               bool tarpit_cancellation, Flusher* flusher) {
  GRPC_TRACE_VLOG(channel, 2)
      << LogTag() << "ServerCallData::Completed: send_trailing_state="
      << StateString(send_trailing_state_) << " send_initial_state="
      << (send_initial_metadata_ == nullptr
              ? "null"
              : SendInitialMetadata::StateString(send_initial_metadata_->state))
      << " error=" << error;
  // Track the latest reason for cancellation.
  cancelled_error_ = error;
  // Stop running the promise.
  promise_ = ArenaPromise<ServerMetadataHandle>();
  switch (send_trailing_state_) {
    case SendTrailingState::kInitial:
    case SendTrailingState::kForwarded:
      send_trailing_state_ = SendTrailingState::kCancelled;
      if (!error.ok()) {
        call_stack()->IncrementRefCount();
        auto* batch = grpc_make_transport_stream_op(
            NewClosure([call_combiner = call_combiner(),
                        call_stack = call_stack()](absl::Status) {
              GRPC_CALL_COMBINER_STOP(call_combiner, "done-cancel");
              call_stack->Unref();
            }));
        batch->cancel_stream = true;
        batch->payload->cancel_stream.cancel_error = error;
        batch->payload->cancel_stream.tarpit = tarpit_cancellation;
        flusher->Resume(batch);
      }
      break;
    case SendTrailingState::kQueued:
      send_trailing_state_ = SendTrailingState::kCancelled;
      send_trailing_metadata_batch_.CancelWith(error, flusher);
      break;
    case SendTrailingState::kQueuedBehindSendMessage:
    case SendTrailingState::kQueuedButHaventClosedSends:
    case SendTrailingState::kCancelled:
      send_trailing_state_ = SendTrailingState::kCancelled;
      break;
  }
  if (send_initial_metadata_ != nullptr) {
    switch (send_initial_metadata_->state) {
      case SendInitialMetadata::kInitial:
      case SendInitialMetadata::kGotPipe:
      case SendInitialMetadata::kForwarded:
      case SendInitialMetadata::kCancelled:
        break;
      case SendInitialMetadata::kQueuedWaitingForPipe:
      case SendInitialMetadata::kQueuedAndGotPipe:
      case SendInitialMetadata::kQueuedAndPushedToPipe:
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
    send_message()->Done(*ServerMetadataFromStatus(error), flusher);
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
  CHECK(recv_initial_state_ == RecvInitialState::kComplete);
  CHECK(std::move(call_args.client_initial_metadata).get() ==
        recv_initial_metadata_);
  forward_recv_initial_metadata_callback_ = true;
  if (send_initial_metadata_ != nullptr) {
    CHECK(send_initial_metadata_->server_initial_metadata_publisher == nullptr);
    CHECK_NE(call_args.server_initial_metadata, nullptr);
    send_initial_metadata_->server_initial_metadata_publisher =
        call_args.server_initial_metadata;
    switch (send_initial_metadata_->state) {
      case SendInitialMetadata::kInitial:
        send_initial_metadata_->state = SendInitialMetadata::kGotPipe;
        break;
      case SendInitialMetadata::kGotPipe:
      case SendInitialMetadata::kQueuedAndGotPipe:
      case SendInitialMetadata::kQueuedAndPushedToPipe:
      case SendInitialMetadata::kForwarded:
        Crash(absl::StrFormat(
            "ILLEGAL STATE: %s",
            SendInitialMetadata::StateString(
                send_initial_metadata_->state)));  // not reachable
        break;
      case SendInitialMetadata::kQueuedWaitingForPipe:
        send_initial_metadata_->state = SendInitialMetadata::kQueuedAndGotPipe;
        break;
      case SendInitialMetadata::kCancelled:
        break;
    }
  } else {
    CHECK_EQ(call_args.server_initial_metadata, nullptr);
  }
  if (send_message() != nullptr) {
    send_message()->GotPipe(call_args.server_to_client_messages);
  } else {
    CHECK_EQ(call_args.server_to_client_messages, nullptr);
  }
  if (receive_message() != nullptr) {
    receive_message()->GotPipe(call_args.client_to_server_messages);
  } else {
    CHECK_EQ(call_args.client_to_server_messages, nullptr);
  }
  return ArenaPromise<ServerMetadataHandle>(
      [this]() { return PollTrailingMetadata(); });
}

// Wrapper to make it look like we're calling the next filter as a promise.
// All polls: await sending the trailing metadata, then forward it down the
// stack.
Poll<ServerMetadataHandle> ServerCallData::PollTrailingMetadata() {
  GRPC_TRACE_LOG(channel, INFO)
      << LogTag()
      << " PollTrailingMetadata: " << StateString(send_trailing_state_);
  switch (send_trailing_state_) {
    case SendTrailingState::kInitial:
    case SendTrailingState::kQueuedBehindSendMessage:
    case SendTrailingState::kQueuedButHaventClosedSends:
      return Pending{};
    case SendTrailingState::kQueued:
      return WrapMetadata(send_trailing_metadata_batch_->payload
                              ->send_trailing_metadata.send_trailing_metadata);
    case SendTrailingState::kForwarded:
      Crash(absl::StrFormat("ILLEGAL STATE: %s",
                            StateString(send_trailing_state_)));  // unreachable
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
  GRPC_TRACE_LOG(channel, INFO)
      << LogTag() << ": RecvTrailingMetadataReady error=" << error
      << " md=" << recv_trailing_metadata_->DebugString();
  Flusher flusher(this, GRPC_LATENT_SEE_METADATA(
                            "ServerCallData::RecvTrailingMetadataReady"));
  PollContext poll_ctx(this, &flusher);
  Completed(error, recv_trailing_metadata_->get(GrpcTarPit()).has_value(),
            &flusher);
  flusher.AddClosure(original_recv_trailing_metadata_ready_, std::move(error),
                     "continue recv trailing");
}

void ServerCallData::RecvInitialMetadataReadyCallback(void* arg,
                                                      grpc_error_handle error) {
  static_cast<ServerCallData*>(arg)->RecvInitialMetadataReady(std::move(error));
}

void ServerCallData::RecvInitialMetadataReady(grpc_error_handle error) {
  Flusher flusher(this, GRPC_LATENT_SEE_METADATA(
                            "ServerCallData::RecvInitialMetadataReady"));
  GRPC_TRACE_LOG(channel, INFO)
      << LogTag() << ": RecvInitialMetadataReady " << error;
  CHECK(recv_initial_state_ == RecvInitialState::kForwarded);
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
  ChannelFilter* filter = promise_filter_detail::ChannelFilterFromElem(elem());
  FakeActivity(this).Run([this, filter] {
    promise_ = filter->MakeCallPromise(
        CallArgs{WrapMetadata(recv_initial_metadata_),
                 ClientInitialMetadataOutstandingToken::Empty(), nullptr,
                 server_initial_metadata_pipe() == nullptr
                     ? nullptr
                     : &server_initial_metadata_pipe()->sender,
                 receive_message() == nullptr
                     ? nullptr
                     : receive_message()->interceptor()->original_receiver(),
                 send_message() == nullptr
                     ? nullptr
                     : send_message()->interceptor()->original_sender()},
        [this](CallArgs call_args) {
          return MakeNextPromise(std::move(call_args));
        });
  });
  // Poll once.
  WakeInsideCombiner(&flusher);
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
  GRPC_LATENT_SEE_INNER_SCOPE("ServerCallData::WakeInsideCombiner");
  PollContext poll_ctx(this, flusher);
  GRPC_TRACE_LOG(channel, INFO)
      << LogTag() << ": WakeInsideCombiner " << DebugString();
  poll_ctx.ClearRepoll();
  if (send_initial_metadata_ != nullptr) {
    if (send_initial_metadata_->state ==
        SendInitialMetadata::kQueuedAndGotPipe) {
      send_initial_metadata_->state =
          SendInitialMetadata::kQueuedAndPushedToPipe;
      CHECK(!send_initial_metadata_->metadata_push_.has_value());
      CHECK(!send_initial_metadata_->metadata_next_.has_value());
      send_initial_metadata_->metadata_push_.emplace(
          send_initial_metadata_->server_initial_metadata_publisher->Push(
              ServerMetadataHandle(
                  send_initial_metadata_->batch->payload->send_initial_metadata
                      .send_initial_metadata,
                  Arena::PooledDeleter(nullptr))));
      send_initial_metadata_->metadata_next_.emplace(
          server_initial_metadata_pipe()->receiver.Next());
    }
    if (send_initial_metadata_->metadata_push_.has_value()) {
      if ((*send_initial_metadata_->metadata_push_)().ready()) {
        GRPC_TRACE_LOG(channel, INFO)
            << LogTag() << ": WakeInsideCombiner: metadata_push done";
        send_initial_metadata_->metadata_push_.reset();
      } else {
        GRPC_TRACE_LOG(channel, INFO)
            << LogTag() << ": WakeInsideCombiner: metadata_push pending";
      }
    }
  }
  if (send_message() != nullptr) {
    if (send_trailing_state_ ==
        SendTrailingState::kQueuedButHaventClosedSends) {
      send_trailing_state_ = SendTrailingState::kQueued;
      send_message()->Done(*send_trailing_metadata_batch_->payload
                                ->send_trailing_metadata.send_trailing_metadata,
                           flusher);
    }
    send_message()->WakeInsideCombiner(
        flusher,
        send_initial_metadata_ == nullptr ||
            send_initial_metadata_->state == SendInitialMetadata::kForwarded);
    GRPC_TRACE_VLOG(channel, 2)
        << LogTag() << ": After send_message WakeInsideCombiner "
        << DebugString() << " is_idle=" << send_message()->IsIdle()
        << " is_forwarded=" << send_message()->IsForwarded();
    if (send_trailing_state_ == SendTrailingState::kQueuedBehindSendMessage &&
        (send_message()->IsIdle() ||
         (send_trailing_metadata_batch_->send_message &&
          send_message()->IsForwarded()))) {
      send_trailing_state_ = SendTrailingState::kQueued;
      if (send_trailing_metadata_batch_->payload->send_trailing_metadata
              .send_trailing_metadata->get(GrpcStatusMetadata())
              .value_or(GRPC_STATUS_UNKNOWN) != GRPC_STATUS_OK) {
        send_message()->Done(
            *send_trailing_metadata_batch_->payload->send_trailing_metadata
                 .send_trailing_metadata,
            flusher);
      }
    }
  }
  if (receive_message() != nullptr) {
    receive_message()->WakeInsideCombiner(flusher, true);
  }
  if (promise_.has_value()) {
    Poll<ServerMetadataHandle> poll;
    poll = promise_();
    GRPC_TRACE_LOG(channel, INFO)
        << LogTag() << ": WakeInsideCombiner poll="
        << PollToString(
               poll,
               [](const ServerMetadataHandle& h) { return h->DebugString(); })
               .c_str()
        << "; send_initial_metadata="
        << (send_initial_metadata_ == nullptr
                ? "null"
                : SendInitialMetadata::StateString(
                      send_initial_metadata_->state))
        << " send_trailing_metadata=" << StateString(send_trailing_state_);

    if (send_initial_metadata_ != nullptr &&
        send_initial_metadata_->state ==
            SendInitialMetadata::kQueuedAndPushedToPipe) {
      CHECK(send_initial_metadata_->metadata_next_.has_value());
      auto p = (*send_initial_metadata_->metadata_next_)();
      GRPC_TRACE_LOG(channel, INFO)
          << LogTag() << ": WakeInsideCombiner send_initial_metadata poll="
          << PollToString(p, [](const NextResult<ServerMetadataHandle>& h) {
               return (*h)->DebugString();
             });

      if (auto* nr = p.value_if_ready()) {
        ServerMetadataHandle md = std::move(nr->value());
        if (send_initial_metadata_->batch->payload->send_initial_metadata
                .send_initial_metadata != md.get()) {
          *send_initial_metadata_->batch->payload->send_initial_metadata
               .send_initial_metadata = std::move(*md);
        }
        send_initial_metadata_->state = SendInitialMetadata::kForwarded;
        poll_ctx.Repoll();
        send_initial_metadata_->batch.ResumeWith(flusher);
      }
    }
    if (auto* r = poll.value_if_ready()) {
      promise_ = ArenaPromise<ServerMetadataHandle>();
      auto md = std::move(*r);
      if (send_message() != nullptr) {
        send_message()->Done(*md, flusher);
      }
      if (receive_message() != nullptr) {
        receive_message()->Done(*md, flusher);
      }
      switch (send_trailing_state_) {
        case SendTrailingState::kQueuedBehindSendMessage:
        case SendTrailingState::kQueuedButHaventClosedSends:
        case SendTrailingState::kQueued: {
          if (send_trailing_metadata_batch_->payload->send_trailing_metadata
                  .send_trailing_metadata != md.get()) {
            *send_trailing_metadata_batch_->payload->send_trailing_metadata
                 .send_trailing_metadata = std::move(*md);
          }
          send_trailing_metadata_batch_.ResumeWith(flusher);
          send_trailing_state_ = SendTrailingState::kForwarded;
        } break;
        case SendTrailingState::kForwarded:
          Crash(absl::StrFormat(
              "ILLEGAL STATE: %s",
              StateString(send_trailing_state_)));  // unreachable
          break;
        case SendTrailingState::kInitial: {
          CHECK(*md->get_pointer(GrpcStatusMetadata()) != GRPC_STATUS_OK);
          Completed(StatusFromMetadata(*md), md->get(GrpcTarPit()).has_value(),
                    flusher);
        } break;
        case SendTrailingState::kCancelled:
          // Nothing to do.
          break;
      }
    }
  }
  if (std::exchange(forward_recv_initial_metadata_callback_, false)) {
    if (auto* closure =
            std::exchange(original_recv_initial_metadata_ready_, nullptr)) {
      flusher->AddClosure(closure, absl::OkStatus(),
                          "original_recv_initial_metadata");
    }
  }
}

void ServerCallData::OnWakeup() {
  Flusher flusher(this, GRPC_LATENT_SEE_METADATA("ServerCallData::OnWakeup"));
  ScopedContext context(this);
  WakeInsideCombiner(&flusher);
}

}  // namespace promise_filter_detail
}  // namespace grpc_core
