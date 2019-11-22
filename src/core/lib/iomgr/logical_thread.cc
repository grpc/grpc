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

void LogicalThread::Run(const DebugLocation& location, grpc_closure* closure,
                        grpc_error* error) {
  (void)location;
#ifndef NDEBUG
  closure->file_initiated = location.file();
  closure->line_initiated = location.line();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_logical_thread_trace)) {
    gpr_log(GPR_INFO,
            "LogicalThread::Run() %p Scheduling closure %p: created: [%s:%d], "
            "scheduled [%s:%d]",
            this, closure, closure->file_created, closure->line_created,
            location.file(), location.line());
  }
#endif
  const size_t prev_size = size_.FetchAdd(1);
  if (prev_size == 0) {
    // There is no other closure executing right now on this logical thread.
    // Execute this closure immediately.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_logical_thread_trace)) {
      gpr_log(GPR_INFO, "	Executing immediately");
    }
    closure->cb(closure->cb_arg, error);
    GRPC_ERROR_UNREF(error);
    // Loan this thread to the logical thread and drain the queue
    DrainQueue();
  } else {
    // There already are closures executing on this logical thread. Simply add
    // this closure to the list.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_logical_thread_trace)) {
      gpr_log(GPR_INFO, "	Schedule on list");
    }
    closure->error_data.error = error;
    queue_.Push(closure->next_data.mpscq_node.get());
  }
}

// The thread that calls this loans itself to the logical thread so as to
// execute all the scheduled closures. This is called from within
// LogicalThread::Run() after executing a closure immediately, and hence size_
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
        gpr_log(GPR_INFO, "	Queue Drained");
      }
      break;
    }
    // There is atleast one closure on the queue. Pop the closure from the queue
    // and execute it.
    grpc_closure* closure = nullptr;
    bool empty_unused;
    while ((closure = reinterpret_cast<grpc_closure*>(
                queue_.PopAndCheckEnd(&empty_unused))) == nullptr) {
      // This can happen either due to a race condition within the mpscq
      // implementation or because of a race with Run()
      if (GRPC_TRACE_FLAG_ENABLED(grpc_logical_thread_trace)) {
        gpr_log(GPR_INFO, "	Queue returned nullptr, trying again");
      }
    }
#ifndef NDEBUG
    if (GRPC_TRACE_FLAG_ENABLED(grpc_logical_thread_trace)) {
      gpr_log(GPR_INFO,
              "	Running closure %p: created: [%s:%d], scheduled [%s:%d]",
              closure, closure->file_created, closure->line_created,
              closure->file_initiated, closure->line_initiated);
    }
#endif
    grpc_error* closure_error = closure->error_data.error;
    closure->cb(closure->cb_arg, closure_error);
    GRPC_ERROR_UNREF(closure_error);
  }
}
}  // namespace grpc_core