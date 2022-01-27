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

#include "src/core/lib/iomgr/executor/mpmcqueue.h"

namespace grpc_core {

DebugOnlyTraceFlag grpc_thread_pool_trace(false, "thread_pool");

inline void* InfLenFIFOQueue::PopFront() {
  // Caller should already check queue is not empty and has already held the
  // mutex. This function will assume that there is at least one element in the
  // queue (i.e. queue_head_->content is valid).
  void* result = queue_head_->content;
  count_.store(count_.load(std::memory_order_relaxed) - 1,
               std::memory_order_relaxed);

  // Updates Stats when trace flag turned on.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_thread_pool_trace)) {
    gpr_timespec wait_time =
        gpr_time_sub(gpr_now(GPR_CLOCK_MONOTONIC), queue_head_->insert_time);
    stats_.num_completed++;
    stats_.total_queue_time = gpr_time_add(stats_.total_queue_time, wait_time);
    stats_.max_queue_time = gpr_time_max(
        gpr_convert_clock_type(stats_.max_queue_time, GPR_TIMESPAN), wait_time);

    if (count_.load(std::memory_order_relaxed) == 0) {
      stats_.busy_queue_time =
          gpr_time_add(stats_.busy_queue_time,
                       gpr_time_sub(gpr_now(GPR_CLOCK_MONOTONIC), busy_time));
    }

    gpr_log(GPR_INFO,
            "[InfLenFIFOQueue PopFront] num_completed:        %" PRIu64
            " total_queue_time: %f max_queue_time:   %f busy_queue_time:   %f",
            stats_.num_completed,
            gpr_timespec_to_micros(stats_.total_queue_time),
            gpr_timespec_to_micros(stats_.max_queue_time),
            gpr_timespec_to_micros(stats_.busy_queue_time));
  }

  queue_head_ = queue_head_->next;
  // Signal waiting thread
  if (count_.load(std::memory_order_relaxed) > 0) {
    TopWaiter()->cv.Signal();
  }

  return result;
}

InfLenFIFOQueue::Node* InfLenFIFOQueue::AllocateNodes(int num) {
  num_nodes_ = num_nodes_ + num;
  Node* new_chunk = new Node[num];
  new_chunk[0].next = &new_chunk[1];
  new_chunk[num - 1].prev = &new_chunk[num - 2];
  for (int i = 1; i < num - 1; ++i) {
    new_chunk[i].prev = &new_chunk[i - 1];
    new_chunk[i].next = &new_chunk[i + 1];
  }
  return new_chunk;
}

InfLenFIFOQueue::InfLenFIFOQueue() {
  delete_list_size_ = kDeleteListInitSize;
  delete_list_ = new Node*[delete_list_size_];

  Node* new_chunk = AllocateNodes(kQueueInitNumNodes);
  delete_list_[delete_list_count_++] = new_chunk;
  queue_head_ = queue_tail_ = new_chunk;
  new_chunk[0].prev = &new_chunk[kQueueInitNumNodes - 1];
  new_chunk[kQueueInitNumNodes - 1].next = &new_chunk[0];

  waiters_.next = &waiters_;
  waiters_.prev = &waiters_;
}

InfLenFIFOQueue::~InfLenFIFOQueue() {
  GPR_ASSERT(count_.load(std::memory_order_relaxed) == 0);
  for (size_t i = 0; i < delete_list_count_; ++i) {
    delete[] delete_list_[i];
  }
  delete[] delete_list_;
}

void InfLenFIFOQueue::Put(void* elem) {
  MutexLock l(&mu_);

  int curr_count = count_.load(std::memory_order_relaxed);

  if (queue_tail_ == queue_head_ && curr_count != 0) {
    // List is full. Expands list to double size by inserting new chunk of nodes
    Node* new_chunk = AllocateNodes(curr_count);
    delete_list_[delete_list_count_++] = new_chunk;
    // Expands delete list on full.
    if (delete_list_count_ == delete_list_size_) {
      delete_list_size_ = delete_list_size_ * 2;
      delete_list_ = new Node*[delete_list_size_];
    }
    new_chunk[0].prev = queue_tail_->prev;
    new_chunk[curr_count - 1].next = queue_head_;
    queue_tail_->prev->next = new_chunk;
    queue_head_->prev = &new_chunk[curr_count - 1];
    queue_tail_ = new_chunk;
  }
  queue_tail_->content = static_cast<void*>(elem);

  // Updates Stats info
  if (GRPC_TRACE_FLAG_ENABLED(grpc_thread_pool_trace)) {
    stats_.num_started++;
    gpr_log(GPR_INFO, "[InfLenFIFOQueue Put] num_started:        %" PRIu64,
            stats_.num_started);
    auto current_time = gpr_now(GPR_CLOCK_MONOTONIC);
    if (curr_count == 0) {
      busy_time = current_time;
    }
    queue_tail_->insert_time = current_time;
  }

  count_.store(curr_count + 1, std::memory_order_relaxed);
  queue_tail_ = queue_tail_->next;

  TopWaiter()->cv.Signal();
}

void* InfLenFIFOQueue::Get(gpr_timespec* wait_time) {
  MutexLock l(&mu_);

  if (count_.load(std::memory_order_relaxed) == 0) {
    gpr_timespec start_time;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_thread_pool_trace) &&
        wait_time != nullptr) {
      start_time = gpr_now(GPR_CLOCK_MONOTONIC);
    }

    Waiter self;
    PushWaiter(&self);
    do {
      self.cv.Wait(&mu_);
    } while (count_.load(std::memory_order_relaxed) == 0);
    RemoveWaiter(&self);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_thread_pool_trace) &&
        wait_time != nullptr) {
      *wait_time = gpr_time_sub(gpr_now(GPR_CLOCK_MONOTONIC), start_time);
    }
  }
  GPR_DEBUG_ASSERT(count_.load(std::memory_order_relaxed) > 0);
  return PopFront();
}

void InfLenFIFOQueue::PushWaiter(Waiter* waiter) {
  waiter->next = waiters_.next;
  waiter->prev = &waiters_;
  waiter->next->prev = waiter;
  waiter->prev->next = waiter;
}

void InfLenFIFOQueue::RemoveWaiter(Waiter* waiter) {
  GPR_DEBUG_ASSERT(waiter != &waiters_);
  waiter->next->prev = waiter->prev;
  waiter->prev->next = waiter->next;
}

InfLenFIFOQueue::Waiter* InfLenFIFOQueue::TopWaiter() { return waiters_.next; }

}  // namespace grpc_core
