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

#ifndef GRPC_CORE_LIB_PROMISE_MPSC_H
#define GRPC_CORE_LIB_PROMISE_MPSC_H

#include <grpc/support/port_platform.h>

#include <vector>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/wait_set.h"

namespace grpc_core {

namespace mpscpipe_detail {

template <typename T>
class Center : public RefCounted<Center<T>> {
 public:
  explicit Center(size_t max_queued) : max_queued_(max_queued) {}
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

template <typename T>
class MpscSender {
 public:
  MpscSender(const MpscSender&) = delete;
  MpscSender& operator=(const MpscSender&) = delete;
  MpscSender(MpscSender&&) noexcept = default;
  MpscSender& operator=(MpscSender&&) noexcept = default;

  auto Send(T t) {
    return [this, t = std::move(t)]() mutable { return center_->PollSend(t); };
  }

 private:
  friend class MpscReceiver<T>;
  explicit MpscSender(RefCountedPtr<mpscpipe_detail::Center<T>> center)
      : center_(std::move(center)) {}
  RefCountedPtr<mpscpipe_detail::Center<T>> center_;
};

template <typename T>
class MpscReceiver {
 public:
  explicit MpscReceiver(size_t max_buffer_hint)
      : center_(MakeRefCounted<mpscpipe_detail::Center<T>>(
            std::max(static_cast<size_t>(1), max_buffer_hint / 2))) {}
  MpscSender<T> MakeSender() { return MpscSender<T>(center_); }

  auto Next() {
    return [this]() -> Poll<T> {
      if (buffer_it_ != buffer_.end()) {
        return Poll<T>(std::move(*buffer_it_++));
      }
      if (center_->PollReceiveBatch(buffer_)) {
        buffer_it_ = buffer_.begin();
        return Poll<T>(std::move(*buffer_it_++));
      }
      return Poll<T>::Pending();
    };
  }

 private:
  std::vector<T> buffer_;
  typename std::vector<T>::const_iterator buffer_it_ = buffer_.end();
  RefCountedPtr<mpscpipe_detail::Center<T>> center_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_MPSC_H
