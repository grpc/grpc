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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/threadpool/mpmcqueue.h"

#include <inttypes.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {

DebugOnlyTraceFlag thread_pool_trace(false, "thread_pool_trace");

inline void* MPMCQueue::PopFront() {
  void* result = queue_head_->content;
  Node* head_to_remove = queue_head_;
  queue_head_ = queue_head_->next;

  count_.Store(count_.Load(MemoryOrder::RELAXED) - 1, MemoryOrder::RELAXED);
  gpr_timespec wait_time =
      gpr_time_sub(gpr_now(GPR_CLOCK_PRECISE), head_to_remove->insert_time);

  delete head_to_remove;

  // Update Stats info
  stats_.num_completed++;
  stats_.total_queue_cycles =
      gpr_time_add(stats_.total_queue_cycles, wait_time);
  stats_.max_queue_cycles = gpr_time_max(
      gpr_convert_clock_type(stats_.max_queue_cycles, GPR_TIMESPAN), wait_time);

  if (count_.Load(MemoryOrder::RELAXED) == 0) {
    stats_.busy_time_cycles =
        gpr_time_add(stats_.busy_time_cycles,
                     gpr_time_sub(gpr_now(GPR_CLOCK_PRECISE), busy_time));
  }

  if (GRPC_TRACE_FLAG_ENABLED(thread_pool_trace)) {
    PrintStats();
  }

  // Singal waiting thread
  if (count_.Load(MemoryOrder::RELAXED) > 0 && num_waiters_ > 0) {
    wait_nonempty_.Signal();
  }

  return result;
}

MPMCQueue::MPMCQueue()
    : num_waiters_(0), queue_head_(nullptr), queue_tail_(nullptr) {
  count_.Store(0, MemoryOrder::RELAXED);
}

MPMCQueue::~MPMCQueue() {
  GPR_ASSERT(count_.Load(MemoryOrder::RELAXED) == 0);
  MutexLock l(&mu_);
  GPR_ASSERT(num_waiters_ == 0);
  if (GRPC_TRACE_FLAG_ENABLED(thread_pool_trace)) {
    PrintStats();
  }
}

void MPMCQueue::Put(void* elem) {
  MutexLock l(&mu_);

  Node* new_node = static_cast<Node*>(new Node(elem));
  if (count_.Load(MemoryOrder::RELAXED) == 0) {
    busy_time = gpr_now(GPR_CLOCK_PRECISE);
    queue_head_ = queue_tail_ = new_node;
  } else {
    queue_tail_->next = new_node;
    queue_tail_ = queue_tail_->next;
  }
  count_.Store(count_.Load(MemoryOrder::RELAXED) + 1, MemoryOrder::RELAXED);

  // Update Stats info
  stats_.num_started++;
  if (GRPC_TRACE_FLAG_ENABLED(thread_pool_trace)) {
    PrintStats();
  }

  if (num_waiters_ > 0) {
    wait_nonempty_.Signal();
  }
}

void* MPMCQueue::Get() {
  MutexLock l(&mu_);
  if (count_.Load(MemoryOrder::RELAXED) == 0) {
    num_waiters_++;
    do {
      wait_nonempty_.Wait(&mu_);
    } while (count_.Load(MemoryOrder::RELAXED) == 0);
    num_waiters_--;
  }
  GPR_ASSERT(count_.Load(MemoryOrder::RELAXED) > 0);
  return PopFront();
}

void MPMCQueue::PrintStats() {
  gpr_log(GPR_INFO, "STATS INFO:");
  gpr_log(GPR_INFO, "num_started:        %" PRIu64, stats_.num_started);
  gpr_log(GPR_INFO, "num_completed:      %" PRIu64, stats_.num_completed);
  gpr_log(GPR_INFO, "total_queue_cycles: %" PRId32,
          gpr_time_to_millis(stats_.total_queue_cycles));
  gpr_log(GPR_INFO, "max_queue_cycles:   %" PRId32,
          gpr_time_to_millis(stats_.max_queue_cycles));
  gpr_log(GPR_INFO, "busy_time_cycles:   %" PRId32,
          gpr_time_to_millis(stats_.busy_time_cycles));
}

}  // namespace grpc_core
