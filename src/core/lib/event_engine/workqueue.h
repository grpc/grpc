// Copyright 2022 The gRPC Authors
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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_WORKQUEUE_H
#define GRPC_CORE_LIB_EVENT_ENGINE_WORKQUEUE_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <atomic>
#include <deque>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_event_engine {
namespace experimental {

// A fast work queue based lightly on an internal Google implementation.
//
// This uses atomics to access the most recent element in the queue, making it
// fast for LIFO operations. Accessing the oldest (next) element requires taking
// a mutex lock.
template <typename T>
class WorkQueue {
 public:
  // comparable to Timestamp::milliseconds_after_process_epoch()
  static const int64_t kInvalidTimestamp;

  class Storage {
   public:
    Storage() = default;
    Storage(T element, int64_t enqueued)
        : element_(element), enqueued_(enqueued) {}
    ~Storage() = default;
    // not copyable
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    // moveable
    Storage(Storage&& other) noexcept
        : element_(other.element_), enqueued_(other.enqueued_) {}
    Storage& operator=(Storage&& other) noexcept {
      std::swap(element_, other.element_);
      std::swap(enqueued_, other.enqueued_);
      return *this;
    }
    int64_t enqueued() const { return enqueued_; }
    T&& TakeElement() { return std::move(element_); }

   private:
    T element_;
    int64_t enqueued_ = kInvalidTimestamp;
  };

  WorkQueue() = default;

  // Returns whether the queue is empty
  bool Empty() const {
    return (most_recent_element_enqueue_timestamp_.load(
                std::memory_order_relaxed) == kInvalidTimestamp &&
            oldest_enqueued_timestamp_.load(std::memory_order_relaxed) ==
                kInvalidTimestamp);
  }

  // Returns the number of elements in the queue
  // This method locks the queue, be mindful of performance when using it.
  size_t Size() {
    grpc_core::MutexLock lock(&mu_);
    return elements_.size() +
           (most_recent_element_enqueue_timestamp_.load(
                std::memory_order_relaxed) == kInvalidTimestamp
                ? 0
                : 1);
  }
  // Returns the Timestamp of when the most recently-added element was
  // enqueued.
  grpc_core::Timestamp OldestEnqueuedTimestamp() const {
    int64_t front_of_queue_timestamp =
        oldest_enqueued_timestamp_.load(std::memory_order_relaxed);
    if (front_of_queue_timestamp != kInvalidTimestamp) {
      return grpc_core::Timestamp::FromMillisecondsAfterProcessEpoch(
          front_of_queue_timestamp);
    }
    int64_t most_recent_millis =
        most_recent_element_enqueue_timestamp_.load(std::memory_order_relaxed);
    if (most_recent_millis == kInvalidTimestamp) {
      return grpc_core::Timestamp::InfPast();
    }
    return grpc_core::Timestamp::FromMillisecondsAfterProcessEpoch(
        most_recent_millis);
  }
  // Returns the next (oldest) element from the queue, or nullopt if empty
  absl::optional<T> PopFront() ABSL_LOCKS_EXCLUDED(mu_) {
    if (oldest_enqueued_timestamp_.load(std::memory_order_relaxed) !=
        kInvalidTimestamp) {
      absl::optional<T> t = TryLockAndPop(/*front=*/true);
      if (t.has_value()) return t;
    }
    if (most_recent_element_enqueue_timestamp_.load(
            std::memory_order_relaxed) != kInvalidTimestamp) {
      return TryPopMostRecentElement();
    }
    return absl::nullopt;
  }
  // Returns the most recent element from the queue, or nullopt if empty
  absl::optional<T> PopBack() {
    if (most_recent_element_enqueue_timestamp_.load(
            std::memory_order_relaxed) != kInvalidTimestamp) {
      return TryPopMostRecentElement();
    }
    if (oldest_enqueued_timestamp_.load(std::memory_order_relaxed) !=
        kInvalidTimestamp) {
      absl::optional<T> t = TryLockAndPop(/*front=*/false);
      if (t.has_value()) return *t;
    }
    return absl::nullopt;
  }
  // Adds an element to the back of the queue
  void Add(T element) {
    grpc_core::ExecCtx exec_ctx;
    T previous_most_recent;
    int64_t previous_ts;
    {
      absl::optional<T> tmp_element;
      int64_t now = exec_ctx.Now().milliseconds_after_process_epoch();
      {
        grpc_core::MutexLock lock(&most_recent_element_lock_);
        tmp_element = std::exchange(most_recent_element_, element);
        previous_ts = most_recent_element_enqueue_timestamp_.exchange(
            now, std::memory_order_relaxed);
      }
      if (!tmp_element.has_value() || previous_ts == kInvalidTimestamp) return;
      previous_most_recent = std::move(*tmp_element);
    }
    grpc_core::MutexLock lock(&mu_);
    if (elements_.empty()) {
      oldest_enqueued_timestamp_.store(previous_ts, std::memory_order_relaxed);
    }
    elements_.push_back(Storage{std::move(previous_most_recent), previous_ts});
  }

 private:
  // Attempts to pop from the front of the queue (oldest).
  // This will return nullopt if the queue is empty, or if other workers
  // are already attempting to pop from this queue.
  absl::optional<T> TryLockAndPop(bool front) ABSL_LOCKS_EXCLUDED(mu_) {
    // Do not block the worker if there are other workers trying to pop
    // tasks from this queue.
    if (!mu_.TryLock()) return absl::nullopt;
    auto mu_cleanup = absl::MakeCleanup([this]() {
      mu_.AssertHeld();
      mu_.Unlock();
    });
    if (GPR_UNLIKELY(elements_.empty())) {
      if (most_recent_element_enqueue_timestamp_.load(
              std::memory_order_relaxed) == kInvalidTimestamp) {
        return absl::nullopt;
      }
      if (!most_recent_element_lock_.TryLock()) return absl::nullopt;
      absl::optional<T> ret = absl::nullopt;
      if (GPR_LIKELY(most_recent_element_.has_value())) {
        most_recent_element_enqueue_timestamp_.store(kInvalidTimestamp,
                                                     std::memory_order_relaxed);
        ret = std::exchange(most_recent_element_, absl::nullopt);
      }
      most_recent_element_lock_.Unlock();
      return ret;
    }
    // the queue has elements, let's pop one and update timestamps
    Storage ret_s;
    if (front) {
      ret_s = std::move(elements_.front());
      elements_.pop_front();
    } else {
      ret_s = std::move(elements_.back());
      elements_.pop_back();
    }
    if (elements_.empty()) {
      oldest_enqueued_timestamp_.store(kInvalidTimestamp,
                                       std::memory_order_relaxed);
    } else if (front) {
      oldest_enqueued_timestamp_.store(elements_.front().enqueued(),
                                       std::memory_order_relaxed);
    }
    return ret_s.TakeElement();
  }

  // Attempts to pop from the back of the queue (most recent).
  // This will return nullopt if the queue is empty, or if other workers
  // are already attempting to pop from this queue.
  absl::optional<T> TryPopMostRecentElement() {
    if (!most_recent_element_lock_.TryLock()) return absl::nullopt;
    if (GPR_UNLIKELY(!most_recent_element_.has_value())) {
      most_recent_element_lock_.Unlock();
      return absl::nullopt;
    }
    most_recent_element_enqueue_timestamp_.store(kInvalidTimestamp,
                                                 std::memory_order_relaxed);
    absl::optional<T> tmp = std::exchange(most_recent_element_, absl::nullopt);
    most_recent_element_lock_.Unlock();
    return tmp;
  }

  // The managed items in the queue
  std::deque<Storage> elements_ ABSL_GUARDED_BY(mu_);
  // The most recently enqueued element. This is reserved from work stealing
  absl::optional<T> most_recent_element_
      ABSL_GUARDED_BY(most_recent_element_lock_);
  grpc_core::Mutex ABSL_ACQUIRED_AFTER(mu_) most_recent_element_lock_;
  // TODO(hork): consider ABSL_CACHELINE_ALIGNED
  std::atomic<int64_t> most_recent_element_enqueue_timestamp_{
      kInvalidTimestamp};
  std::atomic<int64_t> oldest_enqueued_timestamp_{kInvalidTimestamp};
  grpc_core::Mutex mu_;
};

template <typename T>
const int64_t WorkQueue<T>::kInvalidTimestamp = -1;

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_WORKQUEUE_H
