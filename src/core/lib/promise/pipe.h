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

#include "absl/types/optional.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace pipe_detail {

template <typename T>
class Push;

template <typename T>
class Next;

}  // namespace pipe_detail

template <typename T>
struct Pipe;

template <typename T>
class PipeSender;

template <typename T>
class PipeReceiver;

// Send end of a Pipe.
template <typename T>
class PipeSender {
 public:
  PipeSender(const PipeSender&) = delete;
  PipeSender& operator=(const PipeSender&) = delete;

  PipeSender(PipeSender&& other) noexcept
      : receiver_(other.receiver_), push_(other.push_) {
    if (receiver_ != nullptr) {
      receiver_->sender_ = this;
      other.receiver_ = nullptr;
    }
    if (push_ != nullptr) {
      push_->sender_ = this;
      other.push_ = nullptr;
    }
  }
  PipeSender& operator=(PipeSender&& other) noexcept {
    if (receiver_ != nullptr) {
      receiver_->sender_ = nullptr;
    }
    if (push_ != nullptr) {
      push_->sender_ = nullptr;
    }
    receiver_ = other.receiver_;
    if (receiver_ != nullptr) {
      receiver_->sender_ = this;
      other.receiver_ = nullptr;
    }
    if (push_ != nullptr) {
      push_->sender_ = this;
      other.push_ = nullptr;
    }
    return *this;
  }

  ~PipeSender() {
    if (receiver_ != nullptr) {
      receiver_->waiting_to_receive_.Wake();
      receiver_->sender_ = nullptr;
    }
    if (push_ != nullptr) {
      push_->sender_ = nullptr;
    }
  }

  // Send a single message along the pipe.
  // Returns a promise that will resolve to a bool - true if the message was
  // sent, false if it could never be sent. Blocks the promise until the
  // receiver is either closed or able to receive another message.
  pipe_detail::Push<T> Push(T value);

 private:
  friend struct Pipe<T>;
  friend class PipeReceiver<T>;
  friend class pipe_detail::Next<T>;
  friend class pipe_detail::Push<T>;
  explicit PipeSender(PipeReceiver<T>* receiver) : receiver_(receiver) {}
  PipeReceiver<T>* receiver_;
  pipe_detail::Push<T>* push_ = nullptr;
};

// Receive end of a Pipe.
template <typename T>
class PipeReceiver {
 public:
  PipeReceiver(const PipeReceiver&) = delete;
  PipeReceiver& operator=(const PipeReceiver&) = delete;

  PipeReceiver(PipeReceiver&& other) noexcept
      : sender_(other.sender_),
        next_(other.next_),
        pending_(std::move(other.pending_)),
        waiting_to_send_(std::move(other.waiting_to_send_)),
        waiting_to_receive_(other.waiting_to_receive_) {
    if (sender_ != nullptr) {
      sender_->receiver_ = this;
      other.sender_ = nullptr;
    }
    if (next_ != nullptr) {
      next_->receiver_ = this;
      other.next_ = nullptr;
    }
  }
  PipeReceiver& operator=(PipeReceiver&& other) noexcept {
    if (sender_ != nullptr) {
      sender_->receiver_ = nullptr;
    }
    if (next_ != nullptr) {
      next_->receiver_ = nullptr;
    }
    sender_ = other.sender_;
    next_ = other.next_;
    pending_ = std::move(other.pending_);
    waiting_to_send_ = std::move(other.waiting_to_send_);
    waiting_to_receive_ = std::move(other.waiting_to_receive_);
    if (sender_ != nullptr) {
      sender_->receiver_ = this;
      other.sender_ = nullptr;
    }
    if (next_ != nullptr) {
      next_->receiver_ = this;
      other.next_ = nullptr;
    }
    return *this;
  }
  ~PipeReceiver() {
    waiting_to_send_.Wake();
    if (sender_ != nullptr) {
      sender_->receiver_ = nullptr;
    }
    if (next_ != nullptr) {
      next_->receiver_ = nullptr;
    }
  }

  // Receive a single message from the pipe.
  // Returns a promise that will resolve to an optional<T> - with a value if a
  // message was received, or no value if the other end of the pipe was closed.
  // Blocks the promise until the receiver is either closed or a message is
  // available.
  pipe_detail::Next<T> Next();

 private:
  friend struct Pipe<T>;
  friend class PipeSender<T>;
  friend class pipe_detail::Next<T>;
  friend class pipe_detail::Push<T>;
  explicit PipeReceiver(PipeSender<T>* sender) : sender_(sender) {}
  PipeSender<T>* sender_;
  pipe_detail::Next<T>* next_ = nullptr;
  absl::optional<T> pending_;
  IntraActivityWaiter waiting_to_send_;
  IntraActivityWaiter waiting_to_receive_;
};

namespace pipe_detail {

// Implementation of PipeSender::Push promise.
template <typename T>
class Push {
 public:
  Push(const Push&) = delete;
  Push& operator=(const Push&) = delete;
  Push(Push&& other) noexcept
      : sender_(other.sender_), push_(std::move(other.push_)) {
    if (sender_ != nullptr) {
      sender_->push_ = this;
      other.sender_ = nullptr;
    }
  }
  Push& operator=(Push&& other) noexcept {
    if (sender_ != nullptr) {
      sender_->push_ = nullptr;
    }
    sender_ = other.sender_;
    push_ = std::move(other.push_);
    if (sender_ != nullptr) {
      sender_->push_ = this;
      other.sender_ = nullptr;
    }
    return *this;
  }

  ~Push() {
    if (sender_ != nullptr) {
      assert(sender_->push_ == this);
      sender_->push_ = nullptr;
    }
  }

  Poll<bool> operator()() {
    auto* receiver = sender_->receiver_;
    if (receiver == nullptr) {
      return ready(false);
    }
    if (receiver->pending_.has_value()) {
      return receiver->waiting_to_send_.pending();
    }
    receiver->pending_ = std::move(push_);
    receiver->waiting_to_receive_.Wake();
    sender_->push_ = nullptr;
    sender_ = nullptr;
    return ready(true);
  }

 private:
  friend class PipeSender<T>;
  Push(PipeSender<T>* sender, T push)
      : sender_(sender), push_(std::move(push)) {
    assert(sender_->push_ == nullptr);
    sender_->push_ = this;
  }
  PipeSender<T>* sender_;
  T push_;
};

// Implementation of PipeReceiver::Next promise.
template <typename T>
class Next {
 public:
  Next(const Next&) = delete;
  Next& operator=(const Next&) = delete;
  Next(Next&& other) noexcept : receiver_(other.receiver_) {
    if (receiver_ != nullptr) {
      receiver_->next_ = this;
      other.receiver_ = nullptr;
    }
  }
  Next& operator=(Next&& other) noexcept {
    if (receiver_ != nullptr) {
      receiver_->next_ = nullptr;
    }
    receiver_ = other.receiver_;
    if (receiver_ != nullptr) {
      receiver_->next_ = this;
      other.receiver_ = nullptr;
    }
    return *this;
  }

  ~Next() {
    if (receiver_ != nullptr) {
      assert(receiver_->next_ == this);
      receiver_->next_ = nullptr;
    }
  }

  Poll<absl::optional<T>> operator()() {
    if (receiver_->pending_.has_value()) {
      auto result = ready(absl::optional<T>(std::move(*receiver_->pending_)));
      receiver_->pending_.reset();
      receiver_->waiting_to_send_.Wake();
      receiver_->next_ = nullptr;
      receiver_ = nullptr;
      return result;
    }
    if (receiver_->sender_ == nullptr) {
      return ready(absl::optional<T>());
    }
    return receiver_->waiting_to_receive_.pending();
  }

 private:
  friend class PipeReceiver<T>;
  explicit Next(PipeReceiver<T>* receiver) : receiver_(receiver) {
    assert(receiver_->next_ == nullptr);
    receiver_->next_ = this;
  }
  PipeReceiver<T>* receiver_;
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
  Pipe() : sender(&receiver), receiver(&sender) {}
  Pipe(const Pipe&) = delete;
  Pipe& operator=(const Pipe&) = delete;

  PipeSender<T> sender;
  PipeReceiver<T> receiver;
};

}  // namespace grpc_core

#endif
