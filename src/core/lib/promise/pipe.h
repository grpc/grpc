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

template <typename T>
class Center {
 public:
  void Unref() {
    if (0 == --refs_) {
      this->~Center();
    }
  }

  Poll<bool> Push(T* value) {
    if (refs_ == 0) return false;
    if (has_value_) return Pending();
    has_value_ = true;
    value_ = std::move(*value);
    return true;
  }

  Poll<absl::optional<T>> Next() {
    if (refs_ == 0) return absl::nullopt;
    if (!has_value_) return Pending();
    has_value_ = false;
    return std::move(value_);
  }

 private:
  T value_;
  bool has_value_ = false;
  uint8_t refs_ = 2;
};

}  // namespace pipe_detail

// Send end of a Pipe.
template <typename T>
class PipeSender {
 public:
  PipeSender(const PipeSender&) = delete;
  PipeSender& operator=(const PipeSender&) = delete;

  PipeSender(PipeSender&& other) noexcept
      : center_(other.center_) {
        other.center_ = nullptr;
  }
  PipeSender& operator=(PipeSender&& other) noexcept {
    if (center_ != nullptr) center_->Unref();
    center_ = other.center_;
    other.center_ = nullptr;
    return *this;
  }

  ~PipeSender() {
    if (center_ != nullptr) center_->Unref();
  }

  // Send a single message along the pipe.
  // Returns a promise that will resolve to a bool - true if the message was
  // sent, false if it could never be sent. Blocks the promise until the
  // receiver is either closed or able to receive another message.
  pipe_detail::Push<T> Push(T value);

 private:
  pipe_detail::Center<T>* center_;
};

// Receive end of a Pipe.
template <typename T>
class PipeReceiver {
 public:
  PipeReceiver(const PipeReceiver&) = delete;
  PipeReceiver& operator=(const PipeReceiver&) = delete;

  PipeReceiver(PipeReceiver&& other) noexcept
      : center_(other.center_) {
        other.center_= nullptr;
  }
  PipeReceiver& operator=(PipeReceiver&& other) noexcept {
    if (center_ != nullptr) center_->Unref();
    center_ = other.center_;
    other.center_ = nullptr;
    return *this;
  }
  ~PipeReceiver() {
    if (center_ != nullptr) center_->Unref();
  }

  // Receive a single message from the pipe.
  // Returns a promise that will resolve to an optional<T> - with a value if a
  // message was received, or no value if the other end of the pipe was closed.
  // Blocks the promise until the receiver is either closed or a message is
  // available.
  pipe_detail::Next<T> Next();

 private:
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
    if (center_ != nullptr) center_->Unref();
    center_ = other.center_;
    other.center_ = nullptr;
    return *this;
  }

  ~Push() {
    if (center_ != nullptr) center_->Unref();
  }

  Poll<bool> operator()() {
    return center_->Push(&push_);
  }

 private:
  Center<T>* center_;
  T push_;
};

// Implementation of PipeReceiver::Next promise.
template <typename T>
class Next {
 public:
  Next(const Next&) = delete;
  Next& operator=(const Next&) = delete;
  Next(Next&& other) noexcept
      : center_(other.center_), polled_(other.polled_) {
        other.center_ = nullptr;
  }
  Next& operator=(Next&& other) noexcept {
    if (center_ != nullptr) center_->Unref();
    center_ = other.center_;
    other.center_ = nullptr;
    return *this;
  }

  ~Next() {
    if (center_ != nullptr) center_->Unref();
  }

  Poll<absl::optional<T>> operator()() {
    polled_ = true;
    return center_->Next();
  }

 private:
 Center<T>* center_;
 bool polled_ = false;
};

}  // namespace pipe_detail

template <typename T>
pipe_detail::Push<T> PipeSender<T>::Push(T value) {
  return pipe_detail::Push<T>(this, std::move(value));
}

template <typename T>
pipe_detail::Next<T> PipeReceiver<T>::Next() {
  return pipe_detail::Next<T>(this);
}

// A Pipe is an intra-Activity communications channel that transmits T's from
// one end to the other.
// It is only safe to use a Pipe within the context of a single Activity.
// No synchronization is performed internally.
template <typename T>
struct Pipe {
  Pipe() {
    pipe_detail::Center<T>* center = GetContext<Arena>()->New<pipe_detail::Center<T>>();
    sender = PipeSender<T>(center);
    receiver = PipeReceiver<T>(center);
  }
  Pipe(const Pipe&) = delete;
  Pipe& operator=(const Pipe&) = delete;
  Pipe(Pipe&&) noexcept = default;
  Pipe& operator=(Pipe&&) noexcept = default;

  PipeSender<T> sender;
  PipeReceiver<T> receiver;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_PIPE_H
