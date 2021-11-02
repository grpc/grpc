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

#include "src/core/lib/iomgr/work_serializer.h"

namespace grpc_core {

DebugOnlyTraceFlag grpc_work_serializer_trace(false, "work_serializer");

struct CallbackWrapper {
  CallbackWrapper(std::function<void()> cb, const grpc_core::DebugLocation& loc)
      : callback(std::move(cb)), location(loc) {}

  MultiProducerSingleConsumerQueue::Node mpscq_node;
  const std::function<void()> callback;
  const DebugLocation location;
};

class WorkSerializer::WorkSerializerImpl : public Orphanable {
 public:
  void Run(std::function<void()> callback,
           const grpc_core::DebugLocation& location);
  void Schedule(std::function<void()> callback,
                const grpc_core::DebugLocation& location);
  void DrainQueue();
  void Orphan() override;

 private:
  // An initial size of 1 keeps track of whether the work serializer has been
  // orphaned.
  std::atomic<size_t> size_{1};
  std::atomic<bool> draining_{false};
  MultiProducerSingleConsumerQueue queue_;
};

void WorkSerializer::WorkSerializerImpl::Run(
    std::function<void()> callback, const grpc_core::DebugLocation& location) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer::Run() %p Scheduling callback [%s:%d]",
            this, location.file(), location.line());
  }
  const size_t prev_size = size_.fetch_add(1, std::memory_order_acq_rel);
  // The work serializer should not have been orphaned.
  GPR_DEBUG_ASSERT(prev_size > 0);
  // If there is no other callback executing right now on this work serializer,
  // try to grab the lock and execute the callback immediately.
  bool expected = false;
  if (prev_size == 1 && draining_.compare_exchange_strong(expected, true)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
      gpr_log(GPR_INFO, "  Executing immediately");
    }
    callback();
    size_.fetch_sub(1, std::memory_order_acq_rel);
    draining_.store(false, std::memory_order_release);
    // Other callbacks might have been added while this callback was executing.
    DrainQueue();
  } else {
    CallbackWrapper* cb_wrapper =
        new CallbackWrapper(std::move(callback), location);
    // There already are closures executing on this work serializer. Simply add
    // this closure to the queue.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
      gpr_log(GPR_INFO, "  Scheduling on queue : item %p", cb_wrapper);
    }
    queue_.Push(&cb_wrapper->mpscq_node);
  }
}

void WorkSerializer::WorkSerializerImpl::Schedule(
    std::function<void()> callback, const grpc_core::DebugLocation& location) {
  CallbackWrapper* cb_wrapper =
      new CallbackWrapper(std::move(callback), location);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer::Run() %p Scheduling callback %p [%s:%d]",
            this, cb_wrapper, location.file(), location.line());
  }
  size_.fetch_add(1, std::memory_order_acq_rel);
  queue_.Push(&cb_wrapper->mpscq_node);
}

void WorkSerializer::WorkSerializerImpl::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer::Orphan() %p", this);
  }
  size_t prev_size = size_.fetch_sub(1, std::memory_order_acq_rel);
  if (prev_size == 1) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
      gpr_log(GPR_INFO, "  Destroying");
    }
    delete this;
  }
}

// The thread that calls this loans itself to the work serializer so as to
// execute all the scheduled callbacks.
void WorkSerializer::WorkSerializerImpl::DrainQueue() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer::DrainQueue() %p", this);
  }
  while (true) {
    // Mark the work serializer as draining as long as there is atleast one
    // callback.
    while (true) {
      bool expected = false;
      if (!draining_.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel,
                                             std::memory_order_relaxed)) {
        // Another thread is currently draining the queue. No need to do
        // anything.
        return;
      }
      if (size_.load(std::memory_order_acquire) == 1) {
        // There is nothing to drain.
        draining_.store(false, std::memory_order_release);
        // Check again before returning in case a callback was scheduled between
        // the load and the store.
        if (size_.load(std::memory_order_acquire) == 1) {
          return;
        } else {
          continue;
        }
      } else {
        break;
      }
    }
    // Drain the queue
    while (true) {
      // There is at least one callback on the queue. Pop the callback from the
      // queue and execute it.
      CallbackWrapper* cb_wrapper = nullptr;
      bool empty_unused;
      while ((cb_wrapper = reinterpret_cast<CallbackWrapper*>(
                  queue_.PopAndCheckEnd(&empty_unused))) == nullptr) {
        // This can happen due to a race condition within the mpscq
        // implementation or because of a race with Run()/Schedule().
        if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
          gpr_log(GPR_INFO, "  Queue returned nullptr, trying again");
        }
      }
      if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
        gpr_log(GPR_INFO, "  Running item %p : callback scheduled at [%s:%d]",
                cb_wrapper, cb_wrapper->location.file(),
                cb_wrapper->location.line());
      }
      cb_wrapper->callback();
      delete cb_wrapper;
      size_t prev_size = size_.fetch_sub(1, std::memory_order_acq_rel);
      GPR_DEBUG_ASSERT(prev_size >= 1);
      // It is possible that while draining the queue, the last callback ended
      // up orphaning the work serializer. In that case, delete the object.
      if (prev_size == 1) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
          gpr_log(GPR_INFO, "  Queue Drained. Destroying");
        }
        delete this;
        return;
      }
      if (prev_size == 2) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
          gpr_log(GPR_INFO, "  Queue Drained");
        }
        break;
      }
    }
    draining_.store(false, std::memory_order_release);
  }
}

// WorkSerializer

WorkSerializer::WorkSerializer()
    : impl_(MakeOrphanable<WorkSerializerImpl>()) {}

WorkSerializer::~WorkSerializer() {}

void WorkSerializer::Run(std::function<void()> callback,
                         const grpc_core::DebugLocation& location) {
  impl_->Run(std::move(callback), location);
}

void WorkSerializer::Schedule(std::function<void()> callback,
                              const grpc_core::DebugLocation& location) {
  impl_->Run(std::move(callback), location);
}

void WorkSerializer::DrainQueue() { impl_->DrainQueue(); }

}  // namespace grpc_core
