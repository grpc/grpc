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

#ifndef GRPC_CORE_LIB_PROMISE_PIPE_H
#define GRPC_CORE_LIB_PROMISE_PIPE_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/intra_activity_waiter.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"

extern grpc_core::DebugOnlyTraceFlag grpc_trace_promise_pipe;

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
  explicit NextResult(pipe_detail::Center<T>* center) : center_(center) {}
  ~NextResult();
  NextResult(const NextResult&) = delete;
  NextResult& operator=(const NextResult&) = delete;
  NextResult(NextResult&& other) noexcept
      : center_(std::exchange(other.center_, nullptr)) {}
  NextResult& operator=(NextResult&& other) noexcept {
    center_ = std::exchange(other.center_, nullptr);
    return *this;
  }

  using value_type = T;

  void reset();
  bool has_value() const;
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

 private:
  pipe_detail::Center<T>* center_;
};

namespace pipe_detail {

template <typename T>
class Push;
template <typename T>
class Next;

// Center sits between a sender and a receiver to provide a one-deep buffer of
// Ts
template <typename T>
class Center {
 public:
  // Initialize with one send ref (held by PipeSender) and one recv ref (held by
  // PipeReceiver)
  Center() {
    send_refs_ = 1;
    recv_refs_ = 1;
    value_state_ = ValueState::kEmpty;
  }

  // Add one ref to the send side of this object, and return this.
  Center* RefSend() {
    if (grpc_trace_promise_pipe.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("RefSend").c_str());
    }
    send_refs_++;
    GPR_ASSERT(send_refs_ != 0);
    return this;
  }

  // Add one ref to the recv side of this object, and return this.
  Center* RefRecv() {
    if (grpc_trace_promise_pipe.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("RefRecv").c_str());
    }
    recv_refs_++;
    GPR_ASSERT(recv_refs_ != 0);
    return this;
  }

  // Drop a send side ref
  // If no send refs remain, wake due to send closure
  // If no refs remain, destroy this object
  void UnrefSend() {
    if (grpc_trace_promise_pipe.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("UnrefSend").c_str());
    }
    GPR_DEBUG_ASSERT(send_refs_ > 0);
    send_refs_--;
    if (0 == send_refs_) {
      on_full_.Wake();
      on_empty_.Wake();
      if (0 == recv_refs_) {
        this->~Center();
      }
    }
  }

  // Drop a recv side ref
  // If no recv refs remain, wake due to recv closure
  // If no refs remain, destroy this object
  void UnrefRecv() {
    if (grpc_trace_promise_pipe.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("UnrefRecv").c_str());
    }
    GPR_DEBUG_ASSERT(recv_refs_ > 0);
    recv_refs_--;
    if (0 == recv_refs_) {
      on_full_.Wake();
      on_empty_.Wake();
      if (0 == send_refs_) {
        this->~Center();
      } else if (value_state_ == ValueState::kReady) {
        ResetValue();
      }
    }
  }

  // Try to push *value into the pipe.
  // Return Pending if there is no space.
  // Return true if the value was pushed.
  // Return false if the recv end is closed.
  Poll<bool> Push(T* value) {
    if (grpc_trace_promise_pipe.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("Push").c_str());
    }
    GPR_DEBUG_ASSERT(send_refs_ != 0);
    if (recv_refs_ == 0) return false;
    if (value_state_ != ValueState::kEmpty) return on_empty_.pending();
    value_state_ = ValueState::kReady;
    value_ = std::move(*value);
    on_full_.Wake();
    return true;
  }

  Poll<bool> PollAck() {
    if (grpc_trace_promise_pipe.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("PollAck").c_str());
    }
    GPR_DEBUG_ASSERT(send_refs_ != 0);
    if (recv_refs_ == 0) return value_state_ == ValueState::kAcked;
    if (value_state_ != ValueState::kAcked) return on_empty_.pending();
    value_state_ = ValueState::kEmpty;
    return true;
  }

  // Try to receive a value from the pipe.
  // Return Pending if there is no value.
  // Return the value if one was retrieved.
  // Return nullopt if the send end is closed and no value had been pushed.
  Poll<NextResult<T>> Next() {
    if (grpc_trace_promise_pipe.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("Next").c_str());
    }
    GPR_DEBUG_ASSERT(recv_refs_ != 0);
    if (value_state_ != ValueState::kReady) {
      if (send_refs_ == 0) return NextResult<T>(nullptr);
      return on_full_.pending();
    }
    return NextResult<T>(RefRecv());
  }

  void AckNext() {
    if (grpc_trace_promise_pipe.enabled()) {
      gpr_log(GPR_INFO, "%s", DebugOpString("AckNext").c_str());
    }
    GPR_DEBUG_ASSERT(value_state_ == ValueState::kReady);
    value_state_ = ValueState::kAcked;
    on_empty_.Wake();
    UnrefRecv();
  }

  T& value() { return value_; }
  const T& value() const { return value_; }

 private:
  std::string DebugTag() {
    return absl::StrCat(Activity::current()->DebugTag(), "PIPE[0x",
                        reinterpret_cast<uintptr_t>(this), "]: ");
  }
  std::string DebugOpString(std::string op) {
    return absl::StrCat(DebugTag(), op, " send_refs=", send_refs_,
                        " recv_refs=", recv_refs_,
                        " value_state=", ValueStateName(value_state_));
  }
  void ResetValue() {
    // Fancy dance to move out of value in the off chance that we reclaim some
    // memory earlier.
    [](T) {}(std::move(value_));
    value_state_ = ValueState::kEmpty;
  }
  // State of value_.
  enum class ValueState : uint8_t {
    // No value is set, it's possible to send.
    kEmpty,
    // Value has been pushed but not acked, it's possible to receive.
    kReady,
    // Value has been received and acked, we can unblock senders and transition
    // to empty.
    kAcked,
  };
  static const char* ValueStateName(ValueState state) {
    switch (state) {
      case ValueState::kEmpty:
        return "kEmpty";
      case ValueState::kReady:
        return "kReady";
      case ValueState::kAcked:
        return "kAcked";
    }
    GPR_UNREACHABLE_CODE(return "unknown");
  }
  T value_;
  // Number of sending objects.
  // 0 => send is closed.
  // 1 ref each for PipeSender and Push.
  uint8_t send_refs_ : 2;
  // Number of receiving objects.
  // 0 => recv is closed.
  // 1 ref each for PipeReceiver, Next, and NextResult.
  uint8_t recv_refs_ : 2;
  // Current state of the value.
  ValueState value_state_ : 2;
  IntraActivityWaiter on_empty_;
  IntraActivityWaiter on_full_;
};

}  // namespace pipe_detail

// Send end of a Pipe.
template <typename T>
class PipeSender {
 public:
  using PushType = pipe_detail::Push<T>;

  PipeSender(const PipeSender&) = delete;
  PipeSender& operator=(const PipeSender&) = delete;

  PipeSender(PipeSender&& other) noexcept : center_(other.center_) {
    other.center_ = nullptr;
  }
  PipeSender& operator=(PipeSender&& other) noexcept {
    if (center_ != nullptr) center_->UnrefSend();
    center_ = other.center_;
    other.center_ = nullptr;
    return *this;
  }

  ~PipeSender() {
    if (center_ != nullptr) center_->UnrefSend();
  }

  void Close() {
    if (auto* center = std::exchange(center_, nullptr)) center->UnrefSend();
  }

  void Swap(PipeSender<T>* other) { std::swap(center_, other->center_); }

  // Send a single message along the pipe.
  // Returns a promise that will resolve to a bool - true if the message was
  // sent, false if it could never be sent. Blocks the promise until the
  // receiver is either closed or able to receive another message.
  PushType Push(T value);

 private:
  friend struct Pipe<T>;
  explicit PipeSender(pipe_detail::Center<T>* center) : center_(center) {}
  pipe_detail::Center<T>* center_;
};

// Receive end of a Pipe.
template <typename T>
class PipeReceiver {
 public:
  using NextType = pipe_detail::Next<T>;

  PipeReceiver(const PipeReceiver&) = delete;
  PipeReceiver& operator=(const PipeReceiver&) = delete;

  PipeReceiver(PipeReceiver&& other) noexcept : center_(other.center_) {
    other.center_ = nullptr;
  }
  PipeReceiver& operator=(PipeReceiver&& other) noexcept {
    if (center_ != nullptr) center_->UnrefRecv();
    center_ = other.center_;
    other.center_ = nullptr;
    return *this;
  }
  ~PipeReceiver() {
    if (center_ != nullptr) center_->UnrefRecv();
  }

  void Swap(PipeReceiver<T>* other) { std::swap(center_, other->center_); }

  // Receive a single message from the pipe.
  // Returns a promise that will resolve to an optional<T> - with a value if a
  // message was received, or no value if the other end of the pipe was closed.
  // Blocks the promise until the receiver is either closed or a message is
  // available.
  NextType Next();

 private:
  friend struct Pipe<T>;
  explicit PipeReceiver(pipe_detail::Center<T>* center) : center_(center) {}
  pipe_detail::Center<T>* center_;
};

namespace pipe_detail {

// Implementation of PipeSender::Push promise.
template <typename T>
class Push {
 public:
  Push(const Push&) = delete;
  Push& operator=(const Push&) = delete;
  Push(Push&& other) noexcept
      : center_(other.center_), push_(std::move(other.push_)) {
    other.center_ = nullptr;
  }
  Push& operator=(Push&& other) noexcept {
    if (center_ != nullptr) center_->UnrefSend();
    center_ = other.center_;
    other.center_ = nullptr;
    push_ = std::move(other.push_);
    return *this;
  }

  ~Push() {
    if (center_ != nullptr) center_->UnrefSend();
  }

  Poll<bool> operator()() {
    if (push_.has_value()) {
      auto r = center_->Push(&*push_);
      if (auto* ok = absl::get_if<bool>(&r)) {
        push_.reset();
        if (!*ok) return false;
      } else {
        return Pending{};
      }
    }
    GPR_DEBUG_ASSERT(!push_.has_value());
    return center_->PollAck();
  }

 private:
  friend class PipeSender<T>;
  explicit Push(pipe_detail::Center<T>* center, T push)
      : center_(center), push_(std::move(push)) {}
  Center<T>* center_;
  absl::optional<T> push_;
};

// Implementation of PipeReceiver::Next promise.
template <typename T>
class Next {
 public:
  Next(const Next&) = delete;
  Next& operator=(const Next&) = delete;
  Next(Next&& other) noexcept : center_(other.center_) {
    other.center_ = nullptr;
  }
  Next& operator=(Next&& other) noexcept {
    if (center_ != nullptr) center_->UnrefRecv();
    center_ = other.center_;
    other.center_ = nullptr;
    return *this;
  }

  ~Next() {
    if (center_ != nullptr) center_->UnrefRecv();
  }

  Poll<NextResult<T>> operator()() {
    auto r = center_->Next();
    if (!absl::holds_alternative<Pending>(r)) {
      std::exchange(center_, nullptr)->UnrefRecv();
    }
    return r;
  }

 private:
  friend class PipeReceiver<T>;
  explicit Next(pipe_detail::Center<T>* center) : center_(center) {}
  Center<T>* center_;
};

}  // namespace pipe_detail

template <typename T>
pipe_detail::Push<T> PipeSender<T>::Push(T value) {
  return pipe_detail::Push<T>(center_->RefSend(), std::move(value));
}

template <typename T>
pipe_detail::Next<T> PipeReceiver<T>::Next() {
  return pipe_detail::Next<T>(center_->RefRecv());
}

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
  if (auto* p = std::exchange(center_, nullptr)) p->AckNext();
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

#endif  // GRPC_CORE_LIB_PROMISE_PIPE_H
