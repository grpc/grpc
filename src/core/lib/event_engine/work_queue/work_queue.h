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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_WORK_QUEUE_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_WORK_QUEUE_H

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

// An interface for thread-safe EventEngine callback work queues.
//
// Implementations should be optimized for LIFO operations using PopMostRecent.
// All methods must be guaranteed thread-safe.
class WorkQueue {
 public:
  // comparable to Timestamp::milliseconds_after_process_epoch()
  static const int64_t kInvalidTimestamp = -1;

  virtual ~WorkQueue() = default;
  // Returns whether the queue is empty.
  virtual bool Empty() const = 0;
  // Returns the size of the queue.
  virtual size_t Size() const = 0;
  // Returns the most recent element from the queue, or nullopt if empty.
  // This is the fastest way to retrieve elements from the queue.
  virtual EventEngine::Closure* PopMostRecent() = 0;
  // Returns the oldest element from the queue, or nullopt if empty.
  virtual EventEngine::Closure* PopOldest() = 0;
  // Adds a closure to the queue.
  virtual void Add(EventEngine::Closure* closure) = 0;
  // Wraps an AnyInvocable and adds it to the the queue.
  virtual void Add(absl::AnyInvocable<void()> invocable) = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_WORK_QUEUE_H
