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

#ifndef GRPC_CORE_LIB_IOMGR_THREADPOOL_MPMCQUEUE_H
#define GRPC_CORE_LIB_IOMGR_THREADPOOL_MPMCQUEUE_H

#include <grpc/support/port_platform.h>

#include <grpc/support/alloc.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {

// Abstract base class of a MPMC queue interface
class MPMCQueueInterface {
 public:
  virtual ~MPMCQueueInterface() {}

  // Put elem into queue immediately at the end of queue.
  // This might cause to block on full queue depending on implementation.
  virtual void Put(void* elem) = 0;

  // Remove the oldest element from the queue and return it.
  // This might cause to block on empty queue depending on implementation.
  virtual void* Get() = 0;

  // Return number of elements in the queue currently
  virtual int count() const = 0;
};

class MPMCQueue : public MPMCQueueInterface {
 public:
  struct Stats {             // Stats of queue
    uint64_t num_started;    // Number of elements have been added to queue
    uint64_t num_completed;  // Number of elements have been removed from
                             // the queue
    gpr_timespec total_queue_cycles;  // Total waiting time that all the
                                      // removed elements have spent in queue
    gpr_timespec max_queue_cycles;    // Max waiting time among all removed
                                      // elements
    gpr_timespec busy_time_cycles;    // Accumulated amount of time that queue
                                      // was not empty

    Stats() {
      num_started = 0;
      num_completed = 0;
      total_queue_cycles = gpr_time_0(GPR_TIMESPAN);
      max_queue_cycles = gpr_time_0(GPR_TIMESPAN);
      busy_time_cycles = gpr_time_0(GPR_TIMESPAN);
    }
  };
  // Create a new Multiple-Producer-Multiple-Consumer Queue. The queue created
  // will have infinite length.
  explicit MPMCQueue();

  // Release all resources hold by the queue. The queue must be empty, and no
  // one waiting on conditional variables.
  ~MPMCQueue();

  // Put elem into queue immediately at the end of queue. Since the queue has
  // infinite length, this routine will never block and should never fail.
  void Put(void* elem);

  // Remove the oldest element from the queue and return it.
  // This routine will cause the thread to block if queue is currently empty.
  void* Get();

  // Return number of elements in queue currently.
  // There might be concurrently add/remove on queue, so count might change
  // quickly.
  int count() const { return count_.Load(MemoryOrder::RELAXED); }

  // Print out Stats. Time measurement are printed in millisecond.
  void PrintStats();

  // Return a copy of current stats info. This info will be changed quickly
  // when queue is still running. This copy will not deleted by queue.
  Stats* queue_stats();

 private:
  void* PopFront();

  struct Node {
    Node* next;                // Linking
    void* content;             // Points to actual element
    gpr_timespec insert_time;  // Time for stats

    Node(void* c) : content(c) {
      next = nullptr;
      insert_time = gpr_now(GPR_CLOCK_PRECISE);
    }
  };

  Mutex mu_;               // Protecting lock
  CondVar wait_nonempty_;  // Wait on empty queue on get
  int num_waiters_;        // Number of waiters

  Node* queue_head_;        // Head of the queue, remove position
  Node* queue_tail_;        // End of queue, insert position
  Atomic<uint64_t> count_;  // Number of elements in queue
  Stats stats_;             // Stats info
  gpr_timespec busy_time;   // Start time of busy queue
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_IOMGR_THREADPOOL_MPMCQUEUE_H */
