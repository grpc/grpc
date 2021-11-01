//
// Copyright 2021 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/exec_ctx_work_serializer.h"

namespace grpc_core {

DebugOnlyTraceFlag grpc_exec_ctx_work_serializer_trace(
    false, "exec_ctx_work_serializer");

namespace {
struct CallbackWrapper {
  CallbackWrapper(std::function<void()> cb, const grpc_core::DebugLocation& loc)
      : callback(std::move(cb)), location(loc) {}

  MultiProducerSingleConsumerQueue::Node mpscq_node;
  const std::function<void()> callback;
  const DebugLocation location;
};
}  // namespace

class ExecCtxWorkSerializer::ExecCtxWorkSerializerImpl : public Orphanable {
 public:
  ExecCtxWorkSerializerImpl() {
    GRPC_CLOSURE_INIT(
        &closure_,
        [](void* arg, grpc_error_handle /* error */) {
          static_cast<ExecCtxWorkSerializerImpl*>(arg)->DrainQueue();
        },
        this, nullptr);
  }

  void Run(std::function<void()> callback,
           const grpc_core::DebugLocation& location);

  void Orphan() override;

 private:
  void DrainQueue();

  // An initial size of 1 keeps track of whether the exec ctx work serializer
  // has been orphaned.
  std::atomic<size_t> size_{1};
  MultiProducerSingleConsumerQueue queue_;
  grpc_closure closure_;
};

void ExecCtxWorkSerializer::ExecCtxWorkSerializerImpl::Run(
    std::function<void()> callback, const grpc_core::DebugLocation& location) {
  CallbackWrapper* cb_wrapper =
      new CallbackWrapper(std::move(callback), location);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_exec_ctx_work_serializer_trace)) {
    gpr_log(GPR_INFO,
            "ExecCtxWorkSerializer::Run() %p Scheduling callback %p [%s:%d]",
            this, cb_wrapper, location.file(), location.line());
  }
  queue_.Push(&cb_wrapper->mpscq_node);
  const size_t prev_size = size_.fetch_add(1);
  // The work serializer should not have been orphaned.
  GPR_DEBUG_ASSERT(prev_size > 0);
  if (prev_size == 1) {
    // This is the first closure on the queue. Schedule the queue to be drained
    // on ExecCtx
    ExecCtx::Run(DEBUG_LOCATION, &closure_, GRPC_ERROR_NONE);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_exec_ctx_work_serializer_trace)) {
      gpr_log(GPR_INFO, "  Begin draining");
    }
  }
}

void ExecCtxWorkSerializer::ExecCtxWorkSerializerImpl::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_exec_ctx_work_serializer_trace)) {
    gpr_log(GPR_INFO, "ExecCtxWorkSerializer::Orphan() %p", this);
  }
  size_t prev_size = size_.fetch_sub(1);
  if (prev_size == 1) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_exec_ctx_work_serializer_trace)) {
      gpr_log(GPR_INFO, "  Destroying");
    }
    delete this;
  }
}

// This is invoked from a thread's ExecCtx
void ExecCtxWorkSerializer::ExecCtxWorkSerializerImpl::DrainQueue() {
  while (true) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_exec_ctx_work_serializer_trace)) {
      gpr_log(GPR_INFO, "ExecCtxWorkSerializer::DrainQueue() %p", this);
    }
    // There is at least one callback on the queue. Pop the callback from the
    // queue and execute it.
    CallbackWrapper* cb_wrapper = nullptr;
    bool empty_unused;
    while ((cb_wrapper = reinterpret_cast<CallbackWrapper*>(
                queue_.PopAndCheckEnd(&empty_unused))) == nullptr) {
      // This can happen either due to a race condition within the mpscq
      // implementation.
      if (GRPC_TRACE_FLAG_ENABLED(grpc_exec_ctx_work_serializer_trace)) {
        gpr_log(GPR_INFO, "  Queue returned nullptr, trying again");
      }
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_exec_ctx_work_serializer_trace)) {
      gpr_log(GPR_INFO, "  Running item %p : callback scheduled at [%s:%d]",
              cb_wrapper, cb_wrapper->location.file(),
              cb_wrapper->location.line());
    }
    cb_wrapper->callback();
    delete cb_wrapper;
    size_t prev_size = size_.fetch_sub(1);
    GPR_DEBUG_ASSERT(prev_size >= 1);
    // It is possible that while draining the queue, one of the callbacks ended
    // up orphaning the exec ctx work serializer. In that case, delete the
    // object.
    if (prev_size == 1) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_exec_ctx_work_serializer_trace)) {
        gpr_log(GPR_INFO, "  Queue Drained. Destroying");
      }
      delete this;
      return;
    }
    if (prev_size == 2) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_exec_ctx_work_serializer_trace)) {
        gpr_log(GPR_INFO, "  Queue Drained");
      }
      return;
    }
  }
}

//
// ExecCtxWorkSerializer
//

ExecCtxWorkSerializer::ExecCtxWorkSerializer()
    : impl_(MakeOrphanable<ExecCtxWorkSerializerImpl>()) {}

ExecCtxWorkSerializer::~ExecCtxWorkSerializer() {}

void ExecCtxWorkSerializer::Run(std::function<void()> callback,
                                const grpc_core::DebugLocation& location) {
  impl_->Run(std::move(callback), location);
}

}  // namespace grpc_core
