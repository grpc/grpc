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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/adaptor.h"
#include "src/core/lib/promise/poll.h"

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

template <class T>
class Promise {
 public:
  virtual Poll<bool> Step(T* output) = 0;
  virtual void Stop() = 0;
};

struct alignas(alignof(void*)) Scratch {
  uint8_t scratch[32];
};

template <typename T>
class FilterInterface {
 public:
  FilterInterface() = default;
  explicit FilterInterface(PipeReceiver<T>* receiver) : receiver_(receiver) {}
  FilterInterface(const FilterInterface&) = delete;
  FilterInterface& operator=(const FilterInterface&) = delete;
  virtual Promise<T>* Step(T* p, Scratch* scratch_space) = 0;
  virtual void Stop() = 0;

 protected:
  friend class PipeReceiver<T>;
  PipeReceiver<T>* receiver_ = nullptr;
  void SetReceiverIndex(int idx, FilterInterface* p);
  char AllocIndex();
};

template <typename T, typename F>
class Filter;

}  // namespace pipe_detail

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
      receiver_->MarkClosed();
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

  // Attach a promise factory based filter to this pipe.
  // The overall promise returned from this will be active until the pipe is
  // closed. If this promise is cancelled before the pipe is closed, the pipe
  // will close. The filter will be run _after_ any other registered filters.
  template <typename F>
  pipe_detail::Filter<T, F> Filter(F f);

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
        filters_(std::move(other.filters_)),
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
    for (auto filter : filters_) {
      filter->receiver_ = this;
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
    filters_ = std::move(other.filters_);
    for (auto filter : filters_) {
      filter->receiver_ = this;
    }
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
    MarkClosed();
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

  // Attach a promise factory based filter to this pipe.
  // The overall promise returned from this will be active until the pipe is
  // closed. If this promise is cancelled before the pipe is closed, the pipe
  // will close. The filter will be run _after_ any other registered filters.
  template <typename F>
  pipe_detail::Filter<T, F> Filter(F f);

 private:
  friend struct Pipe<T>;
  friend class PipeSender<T>;
  friend class pipe_detail::Next<T>;
  friend class pipe_detail::Push<T>;
  friend class pipe_detail::FilterInterface<T>;
  explicit PipeReceiver(PipeSender<T>* sender) : sender_(sender) {}
  PipeSender<T>* sender_;
  pipe_detail::Next<T>* next_ = nullptr;
  absl::InlinedVector<pipe_detail::FilterInterface<T>*, 12> filters_;
  absl::optional<T> pending_;
  IntraActivityWaiter waiting_to_send_;
  IntraActivityWaiter waiting_to_receive_;

  void MarkClosed() {
    if (sender_ == nullptr) {
      return;
    }

    sender_->receiver_ = nullptr;

    waiting_to_receive_.Wake();
    waiting_to_send_.Wake();
    sender_ = nullptr;

    for (auto* filter : filters_) {
      filter->Stop();
    }
  }
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
  Next(Next&& other) noexcept
      : receiver_(other.receiver_),
        next_filter_(other.next_filter_),
        current_promise_(nullptr) {
    assert(other.current_promise_ == nullptr);
    if (receiver_ != nullptr) {
      receiver_->next_ = this;
      other.receiver_ = nullptr;
    }
  }
  Next& operator=(Next&& other) noexcept {
    assert(current_promise_ == nullptr);
    assert(other.current_promise_ == nullptr);
    if (receiver_ != nullptr) {
      receiver_->next_ = nullptr;
    }
    receiver_ = other.receiver_;
    next_filter_ = other.next_filter_;
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
    if (current_promise_ != nullptr) {
      current_promise_->Stop();
    }
  }

  Poll<absl::optional<T>> operator()() {
    if (receiver_->pending_.has_value()) {
      auto* pending = &*receiver_->pending_;
      if (current_promise_ != nullptr) {
        auto r = current_promise_->Step(pending);
        if (auto* p = r.get_ready()) {
          current_promise_->Stop();
          current_promise_ = nullptr;
          if (!*p) {
            receiver_->MarkClosed();
            return ready(absl::optional<T>());
          }
        } else {
          return kPending;
        }
      }
      while (true) {
        if (next_filter_ >= receiver_->filters_.size()) {
          auto result = ready(absl::optional<T>(std::move(*pending)));
          receiver_->pending_.reset();
          receiver_->waiting_to_send_.Wake();
          receiver_->next_ = nullptr;
          receiver_ = nullptr;
          return result;
        }
        current_promise_ =
            receiver_->filters_[next_filter_]->Step(pending, &scratch_);
        next_filter_++;
        if (current_promise_ ==
            reinterpret_cast<Promise<T>*>(uintptr_t(false))) {
          current_promise_ = nullptr;
          receiver_->MarkClosed();
          return ready(absl::optional<T>());
        } else if (current_promise_ ==
                   reinterpret_cast<Promise<T>*>(uintptr_t(true))) {
          current_promise_ = nullptr;
        } else {
          return kPending;
        }
      }
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
  int next_filter_ = 0;
  Promise<T>* current_promise_ = nullptr;
  Scratch scratch_;
};

template <typename T, typename F>
class Filter final : private FilterInterface<T> {
 public:
  Filter(PipeReceiver<T>* receiver, F f)
      : FilterInterface<T>(receiver),
        state_(adaptor_detail::Factory<T, F>(std::move(f))),
        index_(this->AllocIndex()){};
  Filter(absl::Status already_finished) : state_(std::move(already_finished)) {}
  ~Filter() { this->SetReceiverIndex(index_, nullptr); }
  Filter(Filter&& other)
      : FilterInterface<T>(other.receiver_),
        state_(other.state_),
        index_(other.index_) {
    other.receiver_ = nullptr;
    this->SetReceiverIndex(index_, this);
  }

  Filter(const Filter&) = delete;
  Filter& operator=(const Filter&) = delete;

  Poll<absl::Status> operator()() {
    if (auto* state = absl::get_if<1>(&state_)) {
      return ready(std::move(*state));
    }
    return kPending;
  }

 private:
  absl::variant<adaptor_detail::Factory<T, F>, absl::Status> state_;
  const char index_ = -1;

  class PromiseImpl final : public ::grpc_core::pipe_detail::Promise<T> {
    using PF = typename adaptor_detail::Factory<T, F>::Promise;

   public:
    PromiseImpl(PF f, Filter* filter) : f_(std::move(f)), filter_(filter) {}

    Poll<bool> Step(T* output) final {
      auto r = f_();
      if (auto* p = r.get_ready()) {
        if (p->ok()) {
          *output = std::move(**p);
          return ready(true);
        } else {
          filter_->state_.template emplace<absl::Status>(
              std::move(p->status()));
          Activity::WakeupCurrent();
          return ready(false);
        }
      } else {
        return kPending;
      }
    }

    void Stop() final { this->~PromiseImpl(); }

   private:
    PF f_;
    Filter* filter_;
  };

  Promise<T>* Step(T* p, Scratch* scratch) final {
    if (auto* factory = absl::get_if<0>(&state_)) {
      PromiseImpl promise(factory->Repeated(std::move(*p)), this);
      auto r = promise.Step(p);
      if (auto* result = r.get_ready()) {
        return reinterpret_cast<Promise<T>*>(uintptr_t(*result));
      }
      static_assert(sizeof(promise) <= sizeof(Scratch),
                    "scratch size too small");
      static_assert(alignof(decltype(promise)) <= alignof(Scratch),
                    "bad alignment");
      return new (scratch) decltype(promise)(std::move(promise));
    } else {
      return nullptr;
    }
  }

  void Stop() final {
    if (absl::get_if<0>(&state_)) {
      state_.template emplace<absl::Status>(absl::OkStatus());
      Activity::WakeupCurrent();
    }
  }
};

template <typename T>
void FilterInterface<T>::SetReceiverIndex(int idx, FilterInterface* p) {
  if (receiver_) receiver_->filters_[idx] = p;
}

template <typename T>
char FilterInterface<T>::AllocIndex() {
  auto r = receiver_->filters_.size();
  receiver_->filters_.push_back(this);
  return r;
}

}  // namespace pipe_detail

template <typename T>
pipe_detail::Push<T> PipeSender<T>::Push(T value) {
  return pipe_detail::Push<T>(this, std::move(value));
}

template <typename T>
pipe_detail::Next<T> PipeReceiver<T>::Next() {
  return pipe_detail::Next<T>(this);
}

template <typename T>
template <typename F>
pipe_detail::Filter<T, F> PipeSender<T>::Filter(F f) {
  if (receiver_) {
    return pipe_detail::Filter<T, F>(receiver_, std::move(f));
  } else {
    return pipe_detail::Filter<T, F>(absl::OkStatus());
  }
}

template <typename T>
template <typename F>
pipe_detail::Filter<T, F> PipeReceiver<T>::Filter(F f) {
  return pipe_detail::Filter<T, F>(this, std::move(f));
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
  Pipe(Pipe&&) noexcept = default;
  Pipe& operator=(Pipe&&) noexcept = default;

  PipeSender<T> sender;
  PipeReceiver<T> receiver;
};

}  // namespace grpc_core

#endif
