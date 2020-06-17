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

  void Orphan() override;

 private:
  void DrainQueue();

  // An initial size of 1 keeps track of whether the work serializer has been
  // orphaned.
  Atomic<size_t> size_{1};
  MultiProducerSingleConsumerQueue queue_;
};

void WorkSerializer::WorkSerializerImpl::Run(
    std::function<void()> callback, const grpc_core::DebugLocation& location) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer::Run() %p Scheduling callback [%s:%d]",
            this, location.file(), location.line());
  }
  const size_t prev_size = size_.FetchAdd(1);
  // The work serializer should not have been orphaned.
  GPR_DEBUG_ASSERT(prev_size > 0);
  if (prev_size == 1) {
    // There is no other closure executing right now on this work serializer.
    // Execute this closure immediately.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
      gpr_log(GPR_INFO, "  Executing immediately");
    }
    callback();
    // Loan this thread to the work serializer thread and drain the queue.
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

void WorkSerializer::WorkSerializerImpl::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer::Orphan() %p", this);
  }
  size_t prev_size = size_.FetchSub(1);
  if (prev_size == 1) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
      gpr_log(GPR_INFO, "  Destroying");
    }
    delete this;
  }
}

// The thread that calls this loans itself to the work serializer so as to
// execute all the scheduled callback. This is called from within
// WorkSerializer::Run() after executing a callback immediately, and hence size_
// is at least 1.
void WorkSerializer::WorkSerializerImpl::DrainQueue() {
  while (true) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
      gpr_log(GPR_INFO, "WorkSerializer::DrainQueue() %p", this);
    }
    size_t prev_size = size_.FetchSub(1);
    GPR_DEBUG_ASSERT(prev_size >= 1);
    // It is possible that while draining the queue, one of the callbacks ended
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
      return;
    }
    // There is at least one callback on the queue. Pop the callback from the
    // queue and execute it.
    CallbackWrapper* cb_wrapper = nullptr;
    bool empty_unused;
    while ((cb_wrapper = reinterpret_cast<CallbackWrapper*>(
                queue_.PopAndCheckEnd(&empty_unused))) == nullptr) {
      // This can happen either due to a race condition within the mpscq
      // implementation or because of a race with Run()
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

}  // namespace grpc_core
