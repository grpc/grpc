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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_WORK_QUEUE_H
#define GRPC_CORE_LIB_EVENT_ENGINE_WORK_QUEUE_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <atomic>
#include <deque>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

// A fast work queue based lightly on an internal Google implementation.
//
// This uses atomics to access the most recent element in the queue, making it
// fast for LIFO operations. Accessing the oldest (next) element requires taking
// a mutex lock.
class WorkQueue {
 public:
  // comparable to Timestamp::milliseconds_after_process_epoch()
  static const int64_t kInvalidTimestamp = -1;

  WorkQueue() = default;
  // Returns whether the queue is empty
  bool Empty() const;
  // Returns the Timestamp of when the most recently-added element was
  // enqueued.
  grpc_core::Timestamp OldestEnqueuedTimestamp() const;
  // Returns the next (oldest) element from the queue, or nullopt if empty
  EventEngine::Closure* PopFront() ABSL_LOCKS_EXCLUDED(mu_);
  // Returns the most recent element from the queue, or nullopt if empty
  EventEngine::Closure* PopBack();
  // Adds a closure to the back of the queue
  void Add(EventEngine::Closure* closure);
  // Wraps an AnyInvocable and adds it to the back of the queue
  void Add(absl::AnyInvocable<void()> invocable);

 private:
  class Storage {
   public:
    Storage() = default;
    // Take a non-owned Closure*
    // Requires an exec_ctx on the stack
    // TODO(ctiller): replace with an alternative time source
    explicit Storage(EventEngine::Closure* closure) noexcept;
    // Wrap an AnyInvocable into a Closure.
    // The closure must be executed or explicitly deleted to prevent memory
    // leaks. Requires an exec_ctx on the stack
    // TODO(ctiller): replace with an alternative time source
    explicit Storage(absl::AnyInvocable<void()> callback) noexcept;
    ~Storage() = default;
    // not copyable
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    // moveable
    Storage(Storage&& other) noexcept;
    Storage& operator=(Storage&& other) noexcept;
    // Is this enqueued?
    int64_t enqueued() const { return enqueued_; }
    // Get the stored closure, or wrapped AnyInvocable
    EventEngine::Closure* closure();

   private:
    EventEngine::Closure* closure_ = nullptr;
    int64_t enqueued_ = kInvalidTimestamp;
  };

  // Attempts to pop from the front of the queue (oldest).
  // This will return nullopt if the queue is empty, or if other workers
  // are already attempting to pop from this queue.
  EventEngine::Closure* TryLockAndPop(bool front) ABSL_LOCKS_EXCLUDED(mu_);
  // Internal implementation, helps with thread safety analysis in TryLockAndPop
  EventEngine::Closure* PopLocked(bool front)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // Attempts to pop from the back of the queue (most recent).
  // This will return nullopt if the queue is empty, or if other workers
  // are already attempting to pop from this queue.
  EventEngine::Closure* TryPopMostRecentElement();
  // Common code for the Add methods
  void AddInternal(Storage&& storage);

  // The managed items in the queue
  std::deque<Storage> elements_ ABSL_GUARDED_BY(mu_);
  // The most recently enqueued element. This is reserved from work stealing
  absl::optional<Storage> most_recent_element_
      ABSL_GUARDED_BY(most_recent_element_lock_);
  grpc_core::Mutex ABSL_ACQUIRED_AFTER(mu_) most_recent_element_lock_;
  // TODO(hork): consider ABSL_CACHELINE_ALIGNED
  std::atomic<int64_t> most_recent_element_enqueue_timestamp_{
      kInvalidTimestamp};
  std::atomic<int64_t> oldest_enqueued_timestamp_{kInvalidTimestamp};
  grpc_core::Mutex mu_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_WORK_QUEUE_H
