/*
 *
 * Copyright 2019 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_IOMGR_EXECUTOR_MPMCQUEUE_H
#define GRPC_CORE_LIB_IOMGR_EXECUTOR_MPMCQUEUE_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gprpp/abstract.h"
#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {

extern DebugOnlyTraceFlag grpc_thread_pool_trace;

// Abstract base class of a Multiple-Producer-Multiple-Consumer(MPMC) queue
// interface
class MPMCQueueInterface {
 public:
  virtual ~MPMCQueueInterface() {}

  // Puts elem into queue immediately at the end of queue.
  // This might cause to block on full queue depending on implementation.
  virtual void Put(void* elem) GRPC_ABSTRACT;

  // Removes the oldest element from the queue and return it.
  // This might cause to block on empty queue depending on implementation.
  virtual void* Get() GRPC_ABSTRACT;

  // Returns number of elements in the queue currently
  virtual int count() const GRPC_ABSTRACT;

  GRPC_ABSTRACT_BASE_CLASS
};

class InfLenFIFOQueue : public MPMCQueueInterface {
 public:
  // Creates a new MPMC Queue. The queue created will have infinite length.
  InfLenFIFOQueue() {}

  // Releases all resources held by the queue. The queue must be empty, and no
  // one waits on conditional variables.
  ~InfLenFIFOQueue();

  // Puts elem into queue immediately at the end of queue. Since the queue has
  // infinite length, this routine will never block and should never fail.
  void Put(void* elem);

  // Removes the oldest element from the queue and returns it.
  // This routine will cause the thread to block if queue is currently empty.
  void* Get();

  // Returns number of elements in queue currently.
  // There might be concurrently add/remove on queue, so count might change
  // quickly.
  int count() const { return count_.Load(MemoryOrder::RELAXED); }

 private:
  // For Internal Use Only.
  // Removes the oldest element from the queue and returns it. This routine
  // will NOT check whether queue is empty, and it will NOT acquire mutex.
  // Caller should do the check and acquire mutex before callling.
  void* PopFront();

  struct Node {
    Node* next;                // Linking
    void* content;             // Points to actual element
    gpr_timespec insert_time;  // Time for stats

    Node(void* c) : content(c) {
      next = nullptr;
      insert_time = gpr_now(GPR_CLOCK_MONOTONIC);
    }
  };

  // Stats of queue. This will only be collect when debug trace mode is on.
  // All printed stats info will have time measurement in microsecond.
  struct Stats {
    uint64_t num_started;    // Number of elements have been added to queue
    uint64_t num_completed;  // Number of elements have been removed from
                             // the queue
    gpr_timespec total_queue_time;  // Total waiting time that all the
                                    // removed elements have spent in queue
    gpr_timespec max_queue_time;    // Max waiting time among all removed
                                    // elements
    gpr_timespec busy_queue_time;   // Accumulated amount of time that queue
                                    // was not empty

    Stats() {
      num_started = 0;
      num_completed = 0;
      total_queue_time = gpr_time_0(GPR_TIMESPAN);
      max_queue_time = gpr_time_0(GPR_TIMESPAN);
      busy_queue_time = gpr_time_0(GPR_TIMESPAN);
    }
  };

  Mutex mu_;               // Protecting lock
  CondVar wait_nonempty_;  // Wait on empty queue on get
  int num_waiters_ = 0;    // Number of waiters

  Node* queue_head_ = nullptr;  // Head of the queue, remove position
  Node* queue_tail_ = nullptr;  // End of queue, insert position
  Atomic<int> count_{0};        // Number of elements in queue
  Stats stats_;                 // Stats info
  gpr_timespec busy_time;       // Start time of busy queue
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_EXECUTOR_MPMCQUEUE_H */
