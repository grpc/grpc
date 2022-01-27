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

class WorkSerializer::WorkSerializerImpl : public Orphanable {
 public:
  void Run(std::function<void()> callback, const DebugLocation& location);
  void Schedule(std::function<void()> callback, const DebugLocation& location);
  void DrainQueue();
  void Orphan() override;

 private:
  struct CallbackWrapper {
    CallbackWrapper(std::function<void()> cb, const DebugLocation& loc)
        : callback(std::move(cb)), location(loc) {}

    MultiProducerSingleConsumerQueue::Node mpscq_node;
    const std::function<void()> callback;
    const DebugLocation location;
  };

  // Callers of DrainQueueOwned should make sure to grab the lock on the
  // workserializer with
  //
  //   prev_ref_pair =
  //     refs_.fetch_add(MakeRefPair(1, 1), std::memory_order_acq_rel);
  //
  // and only invoke DrainQueueOwned() if there was previously no owner. Note
  // that the queue size is also incremented as part of the fetch_add to allow
  // the callers to add a callback to the queue if another thread already holds
  // the lock to the work serializer.
  void DrainQueueOwned();

  // First 16 bits indicate ownership of the WorkSerializer, next 48 bits are
  // queue size (i.e., refs).
  static uint64_t MakeRefPair(uint16_t owners, uint64_t size) {
    GPR_ASSERT(size >> 48 == 0);
    return (static_cast<uint64_t>(owners) << 48) + static_cast<int64_t>(size);
  }
  static uint32_t GetOwners(uint64_t ref_pair) {
    return static_cast<uint32_t>(ref_pair >> 48);
  }
  static uint32_t GetSize(uint64_t ref_pair) {
    return static_cast<uint32_t>(ref_pair & 0xffffffffffffu);
  }

  // An initial size of 1 keeps track of whether the work serializer has been
  // orphaned.
  std::atomic<uint64_t> refs_{MakeRefPair(0, 1)};
  MultiProducerSingleConsumerQueue queue_;
};

void WorkSerializer::WorkSerializerImpl::Run(std::function<void()> callback,
                                             const DebugLocation& location) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer::Run() %p Scheduling callback [%s:%d]",
            this, location.file(), location.line());
  }
  // Increment queue size for the new callback and owner count to attempt to
  // take ownership of the WorkSerializer.
  const uint64_t prev_ref_pair =
      refs_.fetch_add(MakeRefPair(1, 1), std::memory_order_acq_rel);
  // The work serializer should not have been orphaned.
  GPR_DEBUG_ASSERT(GetSize(prev_ref_pair) > 0);
  if (GetOwners(prev_ref_pair) == 0) {
    // We took ownership of the WorkSerializer. Invoke callback and drain queue.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
      gpr_log(GPR_INFO, "  Executing immediately");
    }
    callback();
    DrainQueueOwned();
  } else {
    // Another thread is holding the WorkSerializer, so decrement the ownership
    // count we just added and queue the callback.
    refs_.fetch_sub(MakeRefPair(1, 0), std::memory_order_acq_rel);
    CallbackWrapper* cb_wrapper =
        new CallbackWrapper(std::move(callback), location);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
      gpr_log(GPR_INFO, "  Scheduling on queue : item %p", cb_wrapper);
    }
    queue_.Push(&cb_wrapper->mpscq_node);
  }
}

void WorkSerializer::WorkSerializerImpl::Schedule(
    std::function<void()> callback, const DebugLocation& location) {
  CallbackWrapper* cb_wrapper =
      new CallbackWrapper(std::move(callback), location);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO,
            "WorkSerializer::Schedule() %p Scheduling callback %p [%s:%d]",
            this, cb_wrapper, location.file(), location.line());
  }
  refs_.fetch_add(MakeRefPair(0, 1), std::memory_order_acq_rel);
  queue_.Push(&cb_wrapper->mpscq_node);
}

void WorkSerializer::WorkSerializerImpl::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer::Orphan() %p", this);
  }
  uint64_t prev_ref_pair =
      refs_.fetch_sub(MakeRefPair(0, 1), std::memory_order_acq_rel);
  if (GetSize(prev_ref_pair) == 1) {
    GPR_DEBUG_ASSERT(GetOwners(prev_ref_pair) == 0);
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
  // Attempt to take ownership of the WorkSerializer. Also increment the queue
  // size as required by `DrainQueueOwned()`.
  const uint64_t prev_ref_pair =
      refs_.fetch_add(MakeRefPair(1, 1), std::memory_order_acq_rel);
  if (GetOwners(prev_ref_pair) == 0) {
    // We took ownership of the WorkSerializer. Drain the queue.
    DrainQueueOwned();
  } else {
    // Another thread is holding the WorkSerializer, so decrement the ownership
    // count we just added and queue a no-op callback.
    refs_.fetch_sub(MakeRefPair(1, 0), std::memory_order_acq_rel);
    CallbackWrapper* cb_wrapper = new CallbackWrapper([]() {}, DEBUG_LOCATION);
    queue_.Push(&cb_wrapper->mpscq_node);
  }
}

void WorkSerializer::WorkSerializerImpl::DrainQueueOwned() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer::DrainQueueOwned() %p", this);
  }
  while (true) {
    auto prev_ref_pair = refs_.fetch_sub(MakeRefPair(0, 1));
    // It is possible that while draining the queue, the last callback ended
    // up orphaning the work serializer. In that case, delete the object.
    if (GetSize(prev_ref_pair) == 1) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
        gpr_log(GPR_INFO, "  Queue Drained. Destroying");
      }
      delete this;
      return;
    }
    if (GetSize(prev_ref_pair) == 2) {
      // Queue drained. Give up ownership but only if queue remains empty. Note
      // that we are using relaxed memory order semantics for the load on
      // failure since we don't care about that value.
      uint64_t expected = MakeRefPair(1, 1);
      if (refs_.compare_exchange_strong(expected, MakeRefPair(0, 1),
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
        return;
      }
    }
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
  }
}

//
// WorkSerializer
//

WorkSerializer::WorkSerializer()
    : impl_(MakeOrphanable<WorkSerializerImpl>()) {}

WorkSerializer::~WorkSerializer() {}

void WorkSerializer::Run(std::function<void()> callback,
                         const DebugLocation& location) {
  impl_->Run(std::move(callback), location);
}

void WorkSerializer::Schedule(std::function<void()> callback,
                              const DebugLocation& location) {
  impl_->Schedule(std::move(callback), location);
}

void WorkSerializer::DrainQueue() { impl_->DrainQueue(); }

}  // namespace grpc_core
