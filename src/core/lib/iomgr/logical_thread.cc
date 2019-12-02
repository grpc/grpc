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

#include "src/core/lib/iomgr/logical_thread.h"

namespace grpc_core {

DebugOnlyTraceFlag grpc_logical_thread_trace(false, "logical_thread");

struct CallbackWrapper {
  CallbackWrapper(std::function<void()> cb, const grpc_core::DebugLocation& loc)
      : callback(std::move(cb)), location(loc) {}

  MultiProducerSingleConsumerQueue::Node mpscq_node;
  const std::function<void()> callback;
  const DebugLocation location;
};

void LogicalThread::Run(std::function<void()> callback,
                        const grpc_core::DebugLocation& location) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_logical_thread_trace)) {
    gpr_log(GPR_INFO, "LogicalThread::Run() %p Scheduling callback [%s:%d]",
            this, location.file(), location.line());
  }
  const size_t prev_size = size_.FetchAdd(1);
  if (prev_size == 0) {
    // There is no other closure executing right now on this logical thread.
    // Execute this closure immediately.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_logical_thread_trace)) {
      gpr_log(GPR_INFO, "  Executing immediately");
    }
    callback();
    // Loan this thread to the logical thread and drain the queue.
    DrainQueue();
  } else {
    CallbackWrapper* cb_wrapper =
        new CallbackWrapper(std::move(callback), location);
    // There already are closures executing on this logical thread. Simply add
    // this closure to the queue.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_logical_thread_trace)) {
      gpr_log(GPR_INFO, "  Scheduling on queue : item %p", cb_wrapper);
    }
    queue_.Push(&cb_wrapper->mpscq_node);
  }
}

// The thread that calls this loans itself to the logical thread so as to
// execute all the scheduled callback. This is called from within
// LogicalThread::Run() after executing a callback immediately, and hence size_
// is atleast 1.
void LogicalThread::DrainQueue() {
  while (true) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_logical_thread_trace)) {
      gpr_log(GPR_INFO, "LogicalThread::DrainQueue() %p", this);
    }
    size_t prev_size = size_.FetchSub(1);
    // prev_size should be atleast 1 since
    GPR_DEBUG_ASSERT(prev_size >= 1);
    if (prev_size == 1) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_logical_thread_trace)) {
        gpr_log(GPR_INFO, "  Queue Drained");
      }
      break;
    }
    // There is atleast one callback on the queue. Pop the callback from the
    // queue and execute it.
    CallbackWrapper* cb_wrapper = nullptr;
    bool empty_unused;
    while ((cb_wrapper = reinterpret_cast<CallbackWrapper*>(
                queue_.PopAndCheckEnd(&empty_unused))) == nullptr) {
      // This can happen either due to a race condition within the mpscq
      // implementation or because of a race with Run()
      if (GRPC_TRACE_FLAG_ENABLED(grpc_logical_thread_trace)) {
        gpr_log(GPR_INFO, "  Queue returned nullptr, trying again");
      }
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_logical_thread_trace)) {
      gpr_log(GPR_INFO, "  Running item %p : callback scheduled at [%s:%d]",
              cb_wrapper, cb_wrapper->location.file(),
              cb_wrapper->location.line());
    }
    cb_wrapper->callback();
    delete cb_wrapper;
  }
}
}  // namespace grpc_core
