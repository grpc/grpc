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

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <new>
#include <type_traits>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/support/log.h>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/detail/promise_factory.h"
#include "src/core/lib/promise/intra_activity_waiter.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"

namespace grpc_core {

template <typename T>
struct Pipe;
template <typename T>
class PipeSender;
template <typename T>
class PipeReceiver;

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
    has_value_ = false;
  }

  // Add one ref to the send side of this object, and return this.
  Center* RefSend() {
    send_refs_++;
    return this;
  }

  // Add one ref to the recv side of this object, and return this.
  Center* RefRecv() {
    recv_refs_++;
    return this;
  }

  // Drop a send side ref
  // If no send refs remain, wake due to send closure
  // If no refs remain, destroy this object
  void UnrefSend() {
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
    GPR_DEBUG_ASSERT(recv_refs_ > 0);
    recv_refs_--;
    if (0 == recv_refs_) {
      on_full_.Wake();
      on_empty_.Wake();
      if (0 == send_refs_) {
        this->~Center();
      } else if (has_value_) {
        ResetValue();
      }
    }
  }

  // Try to push *value into the pipe.
  // Return Pending if there is no space.
  // Return true if the value was pushed.
  // Return false if the recv end is closed.
  Poll<bool> Push(T* value) {
    GPR_DEBUG_ASSERT(send_refs_ != 0);
    if (recv_refs_ == 0) return false;
    if (has_value_) return on_empty_.pending();
    has_value_ = true;
    value_ = std::move(*value);
    on_full_.Wake();
    return true;
  }

  // Try to receive a value from the pipe.
  // Return Pending if there is no value.
  // Return the value if one was retrieved.
  // Return nullopt if the send end is closed and no value had been pushed.
  Poll<absl::optional<T>> Next() {
    GPR_DEBUG_ASSERT(recv_refs_ != 0);
    if (!has_value_) {
      if (send_refs_ == 0) return absl::nullopt;
      return on_full_.pending();
    }
    has_value_ = false;
    on_empty_.Wake();
    return std::move(value_);
  }

 private:
  void ResetValue() {
    // Fancy dance to move out of value in the off chance that we reclaim some
    // memory earlier.
    [](T) {}(std::move(value_));
    has_value_ = false;
  }
  T value_;
  // Number of sending objects.
  // 0 => send is closed.
  // 1 ref each for PipeSender and Push.
  uint8_t send_refs_ : 2;
  // Number of receiving objects.
  // 0 => recv is closed.
  // 1 ref each for PipeReceiver and Next.
  uint8_t recv_refs_ : 2;
  // True iff there is a value in the pipe.
  bool has_value_ : 1;
  IntraActivityWaiter on_empty_;
  IntraActivityWaiter on_full_;
};

}  // namespace pipe_detail

// Send end of a Pipe.
template <typename T>
class PipeSender {
 public:
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

  // Send a single message along the pipe.
  // Returns a promise that will resolve to a bool - true if the message was
  // sent, false if it could never be sent. Blocks the promise until the
  // receiver is either closed or able to receive another message.
  pipe_detail::Push<T> Push(T value);

 private:
  friend struct Pipe<T>;
  explicit PipeSender(pipe_detail::Center<T>* center) : center_(center) {}
  pipe_detail::Center<T>* center_;
};

// Receive end of a Pipe.
template <typename T>
class PipeReceiver {
 public:
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

  // Receive a single message from the pipe.
  // Returns a promise that will resolve to an optional<T> - with a value if a
  // message was received, or no value if the other end of the pipe was closed.
  // Blocks the promise until the receiver is either closed or a message is
  // available.
  pipe_detail::Next<T> Next();

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

  Poll<bool> operator()() { return center_->Push(&push_); }

 private:
  friend class PipeSender<T>;
  explicit Push(pipe_detail::Center<T>* center, T push)
      : center_(center), push_(std::move(push)) {}
  Center<T>* center_;
  T push_;
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

  Poll<absl::optional<T>> operator()() { return center_->Next(); }

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
  Pipe() : Pipe(GetContext<Arena>()->New<pipe_detail::Center<T>>()) {}
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
