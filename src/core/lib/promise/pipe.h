// Copyright 2021 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_PIPE_H
#define GRPC_SRC_CORE_LIB_PROMISE_PIPE_H

#include <grpc/support/port_platform.h>

#include <stdint.h>
#include <stdlib.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/interceptor_list.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/trace.h"
#include "src/core/lib/resource_quota/arena.h"

namespace grpc_core {

namespace pipe_detail {
template <typename T>
class Center;
}

template <typename T>
struct Pipe;

// Result of Pipe::Next - represents a received value.
// If has_value() is false, the pipe was closed by the time we polled for the
// next value. No value was received, nor will there ever be.
// This type is movable but not copyable.
// Once the final move is destroyed the pipe will ack the read and unblock the
// send.
template <typename T>
class NextResult final {
 public:
  NextResult() : center_(nullptr) {}
  explicit NextResult(RefCountedPtr<pipe_detail::Center<T>> center)
      : center_(std::move(center)) {
    GPR_ASSERT(center_ != nullptr);
  }
  explicit NextResult(bool cancelled)
      : center_(nullptr), cancelled_(cancelled) {}
  ~NextResult();
  NextResult(const NextResult&) = delete;
  NextResult& operator=(const NextResult&) = delete;
  NextResult(NextResult&& other) noexcept = default;
  NextResult& operator=(NextResult&& other) noexcept = default;

  using value_type = T;

  void reset();
  bool has_value() const;
  // Only valid if has_value()
  const T& value() const {
    GPR_ASSERT(has_value());
    return **this;
  }
  T& value() {
    GPR_ASSERT(has_value());
    return **this;
  }
  const T& operator*() const;
  T& operator*();
  // Only valid if !has_value()
  bool cancelled() { return cancelled_; }

 private:
  RefCountedPtr<pipe_detail::Center<T>> center_;
  bool cancelled_;
};

namespace pipe_detail {

template <typename T>
class Push;
template <typename T>
class Next;

// Center sits between a sender and a receiver to provide a one-deep buffer of
// Ts
template <typename T>
class Center : public InterceptorList<T> {
 public:
  // Initialize with one send ref (held by PipeSender) and one recv ref (held by
  // PipeReceiver)
  Center() {
    refs_ = 2;
    value_state_ = ValueState::kEmpty;
  }

  // Add one ref to this object, and return this.
  void IncrementRefCount() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_DEBUG, "%s", DebugOpString("IncrementRefCount").c_str());
    }
    refs_++;
    GPR_DEBUG_ASSERT(refs_ != 0);
  }

  RefCountedPtr<Center> Ref() {
    IncrementRefCount();
    return RefCountedPtr<Center>(this);
  }

  // Drop a ref
  // If no refs remain, destroy this object
  void Unref() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_DEBUG, "%s", DebugOpString("Unref").c_str());
    }
    GPR_DEBUG_ASSERT(refs_ > 0);
    refs_--;
    if (0 == refs_) {
      this->~Center();
    }
  }

  // Try to push *value into the pipe.
  // Return Pending if there is no space.
  // Return true if the value was pushed.
  // Return false if the recv end is closed.
  Poll<bool> Push(T* value) {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("Push").c_str());
    }
    GPR_DEBUG_ASSERT(refs_ != 0);
    switch (value_state_) {
      case ValueState::kClosed:
      case ValueState::kReadyClosed:
      case ValueState::kCancelled:
      case ValueState::kWaitingForAckAndClosed:
        return false;
      case ValueState::kReady:
      case ValueState::kAcked:
      case ValueState::kWaitingForAck:
        return on_empty_.pending();
      case ValueState::kEmpty:
        value_state_ = ValueState::kReady;
        value_ = std::move(*value);
        on_full_.Wake();
        return true;
    }
    GPR_UNREACHABLE_CODE(return false);
  }

  Poll<bool> PollAck() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("PollAck").c_str());
    }
    GPR_DEBUG_ASSERT(refs_ != 0);
    switch (value_state_) {
      case ValueState::kClosed:
        return true;
      case ValueState::kCancelled:
        return false;
      case ValueState::kReady:
      case ValueState::kReadyClosed:
      case ValueState::kEmpty:
      case ValueState::kWaitingForAck:
      case ValueState::kWaitingForAckAndClosed:
        return on_empty_.pending();
      case ValueState::kAcked:
        value_state_ = ValueState::kEmpty;
        on_empty_.Wake();
        return true;
    }
    return true;
  }

  // Try to receive a value from the pipe.
  // Return Pending if there is no value.
  // Return the value if one was retrieved.
  // Return nullopt if the send end is closed and no value had been pushed.
  Poll<absl::optional<T>> Next() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("Next").c_str());
    }
    GPR_DEBUG_ASSERT(refs_ != 0);
    switch (value_state_) {
      case ValueState::kEmpty:
      case ValueState::kAcked:
      case ValueState::kWaitingForAck:
      case ValueState::kWaitingForAckAndClosed:
        return on_full_.pending();
      case ValueState::kReadyClosed:
        value_state_ = ValueState::kWaitingForAckAndClosed;
        return std::move(value_);
      case ValueState::kReady:
        value_state_ = ValueState::kWaitingForAck;
        return std::move(value_);
      case ValueState::kClosed:
      case ValueState::kCancelled:
        return absl::nullopt;
    }
    GPR_UNREACHABLE_CODE(return absl::nullopt);
  }

  // Check if the pipe is closed for sending (if there is a value still queued
  // but the pipe is closed, reports closed).
  Poll<bool> PollClosedForSender() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("PollClosedForSender").c_str());
    }
    GPR_DEBUG_ASSERT(refs_ != 0);
    switch (value_state_) {
      case ValueState::kEmpty:
      case ValueState::kAcked:
      case ValueState::kReady:
      case ValueState::kWaitingForAck:
        return on_closed_.pending();
      case ValueState::kWaitingForAckAndClosed:
      case ValueState::kReadyClosed:
      case ValueState::kClosed:
        return false;
      case ValueState::kCancelled:
        return true;
    }
    GPR_UNREACHABLE_CODE(return true);
  }

  // Check if the pipe is closed for receiving (if there is a value still queued
  // but the pipe is closed, reports open).
  Poll<bool> PollClosedForReceiver() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("PollClosedForReceiver").c_str());
    }
    GPR_DEBUG_ASSERT(refs_ != 0);
    switch (value_state_) {
      case ValueState::kEmpty:
      case ValueState::kAcked:
      case ValueState::kReady:
      case ValueState::kReadyClosed:
      case ValueState::kWaitingForAck:
      case ValueState::kWaitingForAckAndClosed:
        return on_closed_.pending();
      case ValueState::kClosed:
        return false;
      case ValueState::kCancelled:
        return true;
    }
    GPR_UNREACHABLE_CODE(return true);
  }

  Poll<Empty> PollEmpty() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("PollEmpty").c_str());
    }
    GPR_DEBUG_ASSERT(refs_ != 0);
    switch (value_state_) {
      case ValueState::kReady:
      case ValueState::kReadyClosed:
        return on_empty_.pending();
      case ValueState::kWaitingForAck:
      case ValueState::kWaitingForAckAndClosed:
      case ValueState::kAcked:
      case ValueState::kEmpty:
      case ValueState::kClosed:
      case ValueState::kCancelled:
        return Empty{};
    }
    GPR_UNREACHABLE_CODE(return Empty{});
  }

  void AckNext() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("AckNext").c_str());
    }
    switch (value_state_) {
      case ValueState::kReady:
      case ValueState::kWaitingForAck:
        value_state_ = ValueState::kAcked;
        on_empty_.Wake();
        break;
      case ValueState::kReadyClosed:
      case ValueState::kWaitingForAckAndClosed:
        this->ResetInterceptorList();
        value_state_ = ValueState::kClosed;
        on_closed_.Wake();
        on_empty_.Wake();
        on_full_.Wake();
        break;
      case ValueState::kClosed:
      case ValueState::kCancelled:
        break;
      case ValueState::kEmpty:
      case ValueState::kAcked:
        abort();
    }
  }

  void MarkClosed() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("MarkClosed").c_str());
    }
    switch (value_state_) {
      case ValueState::kEmpty:
      case ValueState::kAcked:
        this->ResetInterceptorList();
        value_state_ = ValueState::kClosed;
        on_empty_.Wake();
        on_full_.Wake();
        on_closed_.Wake();
        break;
      case ValueState::kReady:
        value_state_ = ValueState::kReadyClosed;
        on_closed_.Wake();
        break;
      case ValueState::kWaitingForAck:
        value_state_ = ValueState::kWaitingForAckAndClosed;
        on_closed_.Wake();
        break;
      case ValueState::kReadyClosed:
      case ValueState::kClosed:
      case ValueState::kCancelled:
      case ValueState::kWaitingForAckAndClosed:
        break;
    }
  }

  void MarkCancelled() {
    if (grpc_trace_promise_primitives.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("MarkCancelled").c_str());
    }
    switch (value_state_) {
      case ValueState::kEmpty:
      case ValueState::kAcked:
      case ValueState::kReady:
      case ValueState::kReadyClosed:
      case ValueState::kWaitingForAck:
      case ValueState::kWaitingForAckAndClosed:
        this->ResetInterceptorList();
        value_state_ = ValueState::kCancelled;
        on_empty_.Wake();
        on_full_.Wake();
        on_closed_.Wake();
        break;
      case ValueState::kClosed:
      case ValueState::kCancelled:
        break;
    }
  }

  bool cancelled() { return value_state_ == ValueState::kCancelled; }

  T& value() { return value_; }
  const T& value() const { return value_; }

  std::string DebugTag() {
    if (auto* activity = Activity::current()) {
      return absl::StrCat(activity->DebugTag(), " PIPE[0x", absl::Hex(this),
                          "]: ");
    } else {
      return absl::StrCat("PIPE[0x", reinterpret_cast<uintptr_t>(this), "]: ");
    }
  }

 private:
  // State of value_.
  enum class ValueState : uint8_t {
    // No value is set, it's possible to send.
    kEmpty,
    // Value has been pushed but not acked, it's possible to receive.
    kReady,
    // Value has been read and not acked, both send/receive blocked until ack.
    kWaitingForAck,
    // Value has been received and acked, we can unblock senders and transition
    // to empty.
    kAcked,
    // Pipe is closed successfully, no more values can be sent
    kClosed,
    // Pipe is closed successfully, no more values can be sent
    // (but one value is queued and ready to be received)
    kReadyClosed,
    // Pipe is closed successfully, no more values can be sent
    // (but one value is queued and waiting to be acked)
    kWaitingForAckAndClosed,
    // Pipe is closed unsuccessfully, no more values can be sent
    kCancelled,
  };

  std::string DebugOpString(std::string op) {
    return absl::StrCat(DebugTag(), op, " refs=", refs_,
                        " value_state=", ValueStateName(value_state_),
                        " on_empty=", on_empty_.DebugString().c_str(),
                        " on_full=", on_full_.DebugString().c_str(),
                        " on_closed=", on_closed_.DebugString().c_str());
  }

  static const char* ValueStateName(ValueState state) {
    switch (state) {
      case ValueState::kEmpty:
        return "Empty";
      case ValueState::kReady:
        return "Ready";
      case ValueState::kAcked:
        return "Acked";
      case ValueState::kClosed:
        return "Closed";
      case ValueState::kReadyClosed:
        return "ReadyClosed";
      case ValueState::kWaitingForAck:
        return "WaitingForAck";
      case ValueState::kWaitingForAckAndClosed:
        return "WaitingForAckAndClosed";
      case ValueState::kCancelled:
        return "Cancelled";
    }
    GPR_UNREACHABLE_CODE(return "unknown");
  }

  T value_;
  // Number of refs
  uint8_t refs_;
  // Current state of the value.
  ValueState value_state_;
  IntraActivityWaiter on_empty_;
  IntraActivityWaiter on_full_;
  IntraActivityWaiter on_closed_;

  // Make failure to destruct show up in ASAN builds.
#ifndef NDEBUG
  std::unique_ptr<int> asan_canary_ = std::make_unique<int>(0);
#endif
};

}  // namespace pipe_detail

// Send end of a Pipe.
template <typename T>
class PipeSender {
 public:
  using PushType = pipe_detail::Push<T>;

  PipeSender(const PipeSender&) = delete;
  PipeSender& operator=(const PipeSender&) = delete;
  PipeSender(PipeSender&& other) noexcept = default;
  PipeSender& operator=(PipeSender&& other) noexcept = default;

  ~PipeSender() {
    if (center_ != nullptr) center_->MarkClosed();
  }

  void Close() {
    if (center_ != nullptr) {
      center_->MarkClosed();
      center_.reset();
    }
  }

  void CloseWithError() {
    if (center_ != nullptr) {
      center_->MarkCancelled();
      center_.reset();
    }
  }

  void Swap(PipeSender<T>* other) { std::swap(center_, other->center_); }

  // Send a single message along the pipe.
  // Returns a promise that will resolve to a bool - true if the message was
  // sent, false if it could never be sent. Blocks the promise until the
  // receiver is either closed or able to receive another message.
  PushType Push(T value);

  // Return a promise that resolves when the receiver is closed.
  // The resolved value is a bool - true if the pipe was cancelled, false if it
  // was closed successfully.
  // Checks closed from the senders perspective: that is, if there is a value in
  // the pipe but the pipe is closed, reports closed.
  auto AwaitClosed() {
    return [center = center_]() { return center->PollClosedForSender(); };
  }

  // Interject PromiseFactory f into the pipeline.
  // f will be called with the current value traversing the pipe, and should
  // return a value to replace it with.
  // Interjects at the Push end of the pipe.
  template <typename Fn>
  void InterceptAndMap(Fn f, DebugLocation from = {}) {
    center_->PrependMap(std::move(f), from);
  }

  // Per above, but calls cleanup_fn when the pipe is closed.
  template <typename Fn, typename OnHalfClose>
  void InterceptAndMap(Fn f, OnHalfClose cleanup_fn, DebugLocation from = {}) {
    center_->PrependMapWithCleanup(std::move(f), std::move(cleanup_fn), from);
  }

 private:
  friend struct Pipe<T>;
  explicit PipeSender(pipe_detail::Center<T>* center) : center_(center) {}
  RefCountedPtr<pipe_detail::Center<T>> center_;

  // Make failure to destruct show up in ASAN builds.
#ifndef NDEBUG
  std::unique_ptr<int> asan_canary_ = std::make_unique<int>(0);
#endif
};

template <typename T>
class PipeReceiver;

namespace pipe_detail {

// Implementation of PipeReceiver::Next promise.
template <typename T>
class Next {
 public:
  Next(const Next&) = delete;
  Next& operator=(const Next&) = delete;
  Next(Next&& other) noexcept = default;
  Next& operator=(Next&& other) noexcept = default;

  Poll<absl::optional<T>> operator()() {
    return center_ == nullptr ? absl::nullopt : center_->Next();
  }

 private:
  friend class PipeReceiver<T>;
  explicit Next(RefCountedPtr<Center<T>> center) : center_(std::move(center)) {}

  RefCountedPtr<Center<T>> center_;
};

}  // namespace pipe_detail

// Receive end of a Pipe.
template <typename T>
class PipeReceiver {
 public:
  PipeReceiver(const PipeReceiver&) = delete;
  PipeReceiver& operator=(const PipeReceiver&) = delete;
  PipeReceiver(PipeReceiver&& other) noexcept = default;
  PipeReceiver& operator=(PipeReceiver&& other) noexcept = default;
  ~PipeReceiver() {
    if (center_ != nullptr) center_->MarkCancelled();
  }

  void Swap(PipeReceiver<T>* other) { std::swap(center_, other->center_); }

  // Receive a single message from the pipe.
  // Returns a promise that will resolve to an optional<T> - with a value if a
  // message was received, or no value if the other end of the pipe was closed.
  // Blocks the promise until the receiver is either closed or a message is
  // available.
  auto Next() {
    return Seq(pipe_detail::Next<T>(center_), [center = center_](
                                                  absl::optional<T> value) {
      bool open = value.has_value();
      bool cancelled = center == nullptr ? true : center->cancelled();
      return If(
          open,
          [center = std::move(center), value = std::move(value)]() mutable {
            auto run = center->Run(std::move(value));
            return Map(std::move(run), [center = std::move(center)](
                                           absl::optional<T> value) mutable {
              if (value.has_value()) {
                center->value() = std::move(*value);
                return NextResult<T>(std::move(center));
              } else {
                center->MarkCancelled();
                return NextResult<T>(true);
              }
            });
          },
          [cancelled]() { return NextResult<T>(cancelled); });
    });
  }

  // Return a promise that resolves when the receiver is closed.
  // The resolved value is a bool - true if the pipe was cancelled, false if it
  // was closed successfully.
  // Checks closed from the receivers perspective: that is, if there is a value
  // in the pipe but the pipe is closed, reports open until that value is read.
  auto AwaitClosed() {
    return [center = center_]() { return center->PollClosedForReceiver(); };
  }

  auto AwaitEmpty() {
    return [center = center_]() { return center->PollEmpty(); };
  }

  void CloseWithError() {
    if (center_ != nullptr) {
      center_->MarkCancelled();
      center_.reset();
    }
  }

  // Interject PromiseFactory f into the pipeline.
  // f will be called with the current value traversing the pipe, and should
  // return a value to replace it with.
  // Interjects at the Next end of the pipe.
  template <typename Fn>
  void InterceptAndMap(Fn f, DebugLocation from = {}) {
    center_->AppendMap(std::move(f), from);
  }

  // Per above, but calls cleanup_fn when the pipe is closed.
  template <typename Fn, typename OnHalfClose>
  void InterceptAndMapWithHalfClose(Fn f, OnHalfClose cleanup_fn,
                                    DebugLocation from = {}) {
    center_->AppendMapWithCleanup(std::move(f), std::move(cleanup_fn), from);
  }

 private:
  friend struct Pipe<T>;
  explicit PipeReceiver(pipe_detail::Center<T>* center) : center_(center) {}
  RefCountedPtr<pipe_detail::Center<T>> center_;

  // Make failure to destruct show up in ASAN builds.
#ifndef NDEBUG
  std::unique_ptr<int> asan_canary_ = std::make_unique<int>(0);
#endif
};

namespace pipe_detail {

// Implementation of PipeSender::Push promise.
template <typename T>
class Push {
 public:
  Push(const Push&) = delete;

  Push& operator=(const Push&) = delete;
  Push(Push&& other) noexcept = default;
  Push& operator=(Push&& other) noexcept = default;

  Poll<bool> operator()() {
    if (center_ == nullptr) {
      if (grpc_trace_promise_primitives.enabled()) {
        gpr_log(GPR_DEBUG, "%s Pipe push has a null center",
                Activity::current()->DebugTag().c_str());
      }
      return false;
    }
    if (auto* p = absl::get_if<T>(&state_)) {
      auto r = center_->Push(p);
      if (auto* ok = r.value_if_ready()) {
        state_.template emplace<AwaitingAck>();
        if (!*ok) return false;
      } else {
        return Pending{};
      }
    }
    GPR_DEBUG_ASSERT(absl::holds_alternative<AwaitingAck>(state_));
    return center_->PollAck();
  }

 private:
  struct AwaitingAck {};

  friend class PipeSender<T>;
  explicit Push(RefCountedPtr<pipe_detail::Center<T>> center, T push)
      : center_(std::move(center)), state_(std::move(push)) {}

  RefCountedPtr<Center<T>> center_;
  absl::variant<T, AwaitingAck> state_;
};

}  // namespace pipe_detail

template <typename T>
pipe_detail::Push<T> PipeSender<T>::Push(T value) {
  return pipe_detail::Push<T>(center_ == nullptr ? nullptr : center_->Ref(),
                              std::move(value));
}

template <typename T>
using PipeReceiverNextType = decltype(std::declval<PipeReceiver<T>>().Next());

template <typename T>
bool NextResult<T>::has_value() const {
  return center_ != nullptr;
}

template <typename T>
T& NextResult<T>::operator*() {
  return center_->value();
}

template <typename T>
const T& NextResult<T>::operator*() const {
  return center_->value();
}

template <typename T>
NextResult<T>::~NextResult() {
  if (center_ != nullptr) center_->AckNext();
}

template <typename T>
void NextResult<T>::reset() {
  if (center_ != nullptr) {
    center_->AckNext();
    center_.reset();
  }
}

// A Pipe is an intra-Activity communications channel that transmits T's from
// one end to the other.
// It is only safe to use a Pipe within the context of a single Activity.
// No synchronization is performed internally.
// The primary Pipe data structure is allocated from an arena, so the activity
// must have an arena as part of its context.
// By performing that allocation we can ensure stable pointer to shared data
// allowing PipeSender/PipeReceiver/Push/Next to be relatively simple in their
// implementation.
// This type has been optimized with the expectation that there are relatively
// few pipes per activity. If this assumption does not hold then a design
// allowing inline filtering of pipe contents (instead of connecting pipes with
// polling code) would likely be more appropriate.
template <typename T>
struct Pipe {
  Pipe() : Pipe(GetContext<Arena>()) {}
  explicit Pipe(Arena* arena) : Pipe(arena->New<pipe_detail::Center<T>>()) {}
  Pipe(const Pipe&) = delete;
  Pipe& operator=(const Pipe&) = delete;
  Pipe(Pipe&&) noexcept = default;
  Pipe& operator=(Pipe&&) noexcept = default;

  PipeSender<T> sender;
  PipeReceiver<T> receiver;

 private:
  explicit Pipe(pipe_detail::Center<T>* center)
      : sender(center), receiver(center) {}
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_PIPE_H
