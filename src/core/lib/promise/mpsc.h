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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_MPSC_H
#define GRPC_SRC_CORE_LIB_PROMISE_MPSC_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/wait_set.h"

// Multi producer single consumer inter-activity comms.

namespace grpc_core {

namespace mpscpipe_detail {

// "Center" of the communication pipe.
// Contains sent but not received messages, and open/close state.
template <typename T>
class Center : public RefCounted<Center<T>> {
 public:
  // Construct the center with a maximum queue size.
  explicit Center(size_t max_queued) : max_queued_(max_queued) {}

  // Poll for new items.
  // - Returns true if new items were obtained, in which case they are contained
  //   in dest in the order they were added. Wakes up all pending senders since
  //   there will now be space to send.
  // - If no new items are available, returns
  //   false and sets up a waker to be awoken when more items are available.
  // TODO(ctiller): consider the problem of thundering herds here. There may be
  // more senders than there are queue spots, and so the strategy of waking up
  // all senders is ill-advised.
  // That said, some senders may have been cancelled by the time we wake them,
  // and so waking a subset could cause starvation.
  bool PollReceiveBatch(std::vector<T>& dest) {
    ReleasableMutexLock lock(&mu_);
    if (queue_.empty()) {
      receive_waker_ = Activity::current()->MakeNonOwningWaker();
      return false;
    }
    dest.swap(queue_);
    queue_.clear();
    auto wakeups = send_wakers_.TakeWakeupSet();
    lock.Release();
    wakeups.Wakeup();
    return true;
  }

  // Poll to send one item.
  // Returns pending if no send slot was available.
  // Returns true if the item was sent.
  // Returns false if the receiver has been closed.
  Poll<bool> PollSend(T& t) {
    ReleasableMutexLock lock(&mu_);
    if (receiver_closed_) return Poll<bool>(false);
    if (queue_.size() < max_queued_) {
      queue_.push_back(std::move(t));
      auto receive_waker = std::move(receive_waker_);
      lock.Release();
      receive_waker.Wakeup();
      return Poll<bool>(true);
    }
    send_wakers_.AddPending(Activity::current()->MakeNonOwningWaker());
    return Pending{};
  }

  // Mark that the receiver is closed.
  void ReceiverClosed() {
    MutexLock lock(&mu_);
    receiver_closed_ = true;
  }

 private:
  Mutex mu_;
  const size_t max_queued_;
  std::vector<T> queue_ ABSL_GUARDED_BY(mu_);
  bool receiver_closed_ ABSL_GUARDED_BY(mu_) = false;
  Waker receive_waker_ ABSL_GUARDED_BY(mu_);
  WaitSet send_wakers_ ABSL_GUARDED_BY(mu_);
};

}  // namespace mpscpipe_detail

template <typename T>
class MpscReceiver;

// Send half of an mpsc pipe.
template <typename T>
class MpscSender {
 public:
  MpscSender(const MpscSender&) = delete;
  MpscSender& operator=(const MpscSender&) = delete;
  MpscSender(MpscSender&&) noexcept = default;
  MpscSender& operator=(MpscSender&&) noexcept = default;

  // Return a promise that will send one item.
  // Resolves to true if sent, false if the receiver was closed (and the value
  // will never be successfully sent).
  auto Send(T t) {
    return [this, t = std::move(t)]() mutable { return center_->PollSend(t); };
  }

 private:
  friend class MpscReceiver<T>;
  explicit MpscSender(RefCountedPtr<mpscpipe_detail::Center<T>> center)
      : center_(std::move(center)) {}
  RefCountedPtr<mpscpipe_detail::Center<T>> center_;
};

// Receive half of an mpsc pipe.
template <typename T>
class MpscReceiver {
 public:
  // max_buffer_hint is the maximum number of elements we'd like to buffer.
  // We half this before passing to Center so that the number there is the
  // maximum number of elements that can be queued in the center of the pipe.
  // The receiver also holds some of the buffered elements (up to half of them!)
  // so the total outstanding is equal to max_buffer_hint (unless it's 1 in
  // which case instantaneosly we may have two elements buffered).
  explicit MpscReceiver(size_t max_buffer_hint)
      : center_(MakeRefCounted<mpscpipe_detail::Center<T>>(
            std::max(static_cast<size_t>(1), max_buffer_hint / 2))) {}
  ~MpscReceiver() {
    if (center_ != nullptr) center_->ReceiverClosed();
  }
  MpscReceiver(const MpscReceiver&) = delete;
  MpscReceiver& operator=(const MpscReceiver&) = delete;
  // Only movable until it's first polled, and so we don't need to contend with
  // a non-empty buffer during a legal move!
  MpscReceiver(MpscReceiver&& other) noexcept
      : center_(std::move(other.center_)) {
    GPR_DEBUG_ASSERT(other.buffer_.empty());
  }
  MpscReceiver& operator=(MpscReceiver&& other) noexcept {
    GPR_DEBUG_ASSERT(other.buffer_.empty());
    center_ = std::move(other.center_);
    return *this;
  }

  // Construct a new sender for this receiver.
  MpscSender<T> MakeSender() { return MpscSender<T>(center_); }

  // Return a promise that will resolve to the next item (and remove said item).
  auto Next() {
    return [this]() -> Poll<T> {
      if (buffer_it_ != buffer_.end()) {
        return Poll<T>(std::move(*buffer_it_++));
      }
      if (center_->PollReceiveBatch(buffer_)) {
        buffer_it_ = buffer_.begin();
        return Poll<T>(std::move(*buffer_it_++));
      }
      return Pending{};
    };
  }

 private:
  // Received items. We move out of here one by one, but don't resize the
  // vector. Instead, when we run out of items, we poll the center for more -
  // which swaps this buffer in for the new receive queue and clears it.
  // In this way, upon hitting a steady state the queue ought to be allocation
  // free.
  std::vector<T> buffer_;
  typename std::vector<T>::iterator buffer_it_ = buffer_.end();
  RefCountedPtr<mpscpipe_detail::Center<T>> center_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_MPSC_H
