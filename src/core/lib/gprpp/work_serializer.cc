//
// Copyright 2019 gRPC authors.
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

#include "src/core/lib/gprpp/work_serializer.h"

#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#include "absl/container/inlined_vector.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"

namespace grpc_core {

DebugOnlyTraceFlag grpc_work_serializer_trace(false, "work_serializer");

//
// WorkSerializer::WorkSerializerImpl
//

class WorkSerializer::WorkSerializerImpl {
 public:
  virtual ~WorkSerializerImpl() = default;
  virtual void Run(std::function<void()> callback,
                   const DebugLocation& location) = 0;
  virtual void Schedule(std::function<void()> callback,
                        const DebugLocation& location) = 0;
  virtual void DrainQueue() = 0;
  virtual void Orphan() = 0;

#ifndef NDEBUG
  bool RunningInWorkSerializer() const {
    return std::this_thread::get_id() == current_thread_;
  }
#endif

 protected:
#ifndef NDEBUG
  void SetCurrentThread() { current_thread_ = std::this_thread::get_id(); }
  void ClearCurrentThread() { current_thread_ = std::thread::id(); }
#else
  void SetCurrentThread() {}
  void ClearCurrentThread() {}
#endif

 private:
#ifndef NDEBUG
  std::thread::id current_thread_;
#endif
};

//
// WorkSerializer::LegacyWorkSerializer
//

class WorkSerializer::LegacyWorkSerializer final : public WorkSerializerImpl {
 public:
  void Run(std::function<void()> callback,
           const DebugLocation& location) override;
  void Schedule(std::function<void()> callback,
                const DebugLocation& location) override;
  void DrainQueue() override;
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
  static uint64_t GetSize(uint64_t ref_pair) {
    return static_cast<uint64_t>(ref_pair & 0xffffffffffffu);
  }

  // An initial size of 1 keeps track of whether the work serializer has been
  // orphaned.
  std::atomic<uint64_t> refs_{MakeRefPair(0, 1)};
  MultiProducerSingleConsumerQueue queue_;
};

void WorkSerializer::LegacyWorkSerializer::Run(std::function<void()> callback,
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
    SetCurrentThread();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
      gpr_log(GPR_INFO, "  Executing immediately");
    }
    callback();
    // Delete the callback while still holding the WorkSerializer, so
    // that any refs being held by the callback via lambda captures will
    // be destroyed inside the WorkSerializer.
    callback = nullptr;
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

void WorkSerializer::LegacyWorkSerializer::Schedule(
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

void WorkSerializer::LegacyWorkSerializer::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer::Orphan() %p", this);
  }
  const uint64_t prev_ref_pair =
      refs_.fetch_sub(MakeRefPair(0, 1), std::memory_order_acq_rel);
  if (GetOwners(prev_ref_pair) == 0 && GetSize(prev_ref_pair) == 1) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
      gpr_log(GPR_INFO, "  Destroying");
    }
    delete this;
  }
}

// The thread that calls this loans itself to the work serializer so as to
// execute all the scheduled callbacks.
void WorkSerializer::LegacyWorkSerializer::DrainQueue() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer::DrainQueue() %p", this);
  }
  // Attempt to take ownership of the WorkSerializer. Also increment the queue
  // size as required by `DrainQueueOwned()`.
  const uint64_t prev_ref_pair =
      refs_.fetch_add(MakeRefPair(1, 1), std::memory_order_acq_rel);
  if (GetOwners(prev_ref_pair) == 0) {
    ClearCurrentThread();
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

void WorkSerializer::LegacyWorkSerializer::DrainQueueOwned() {
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
      // Queue drained. Give up ownership but only if queue remains empty.
      // Reset current_thread_ before giving up ownership to avoid TSAN
      // race.  If we don't wind up giving up ownership, we'll set this
      // again below before we pull the next callback out of the queue.
      ClearCurrentThread();
      uint64_t expected = MakeRefPair(1, 1);
      if (refs_.compare_exchange_strong(expected, MakeRefPair(0, 1),
                                        std::memory_order_acq_rel)) {
        // Queue is drained.
        return;
      }
      if (GetSize(expected) == 0) {
        // WorkSerializer got orphaned while this was running
        if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
          gpr_log(GPR_INFO, "  Queue Drained. Destroying");
        }
        delete this;
        return;
      }
      // Didn't wind up giving up ownership, so set current_thread_ again.
      SetCurrentThread();
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
// WorkSerializer::DispatchingWorkSerializer
//

class WorkSerializer::DispatchingWorkSerializer final
    : public WorkSerializerImpl,
      private grpc_event_engine::experimental::EventEngine::Closure {
 public:
  explicit DispatchingWorkSerializer(
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine)
      : event_engine_(std::move(event_engine)) {}
  void Run(std::function<void()> callback,
           const DebugLocation& location) override;
  void Schedule(std::function<void()> callback,
                const DebugLocation& location) override {
    Run(callback, location);
  }
  void DrainQueue() override {}
  void Orphan() override;

 private:
  struct CallbackWrapper {
    CallbackWrapper(std::function<void()> cb, const DebugLocation& loc)
        : callback(std::move(cb)), location(loc) {}
    std::function<void()> callback;
    GPR_NO_UNIQUE_ADDRESS DebugLocation location;
  };
  using CallbackVector = absl::InlinedVector<CallbackWrapper, 1>;

  void Run() override;

  CallbackVector processing_;
  const std::shared_ptr<grpc_event_engine::experimental::EventEngine>
      event_engine_;
  bool running_ ABSL_GUARDED_BY(mu_) = false;
  bool orphaned_ ABSL_GUARDED_BY(mu_) = false;
  Mutex mu_;
  CallbackVector incoming_ ABSL_GUARDED_BY(mu_);
};

void WorkSerializer::DispatchingWorkSerializer::Orphan() {
  ReleasableMutexLock lock(&mu_);
  if (!running_) {
    lock.Release();
    delete this;
    return;
  }
  orphaned_ = true;
}

void WorkSerializer::DispatchingWorkSerializer::Run(
    std::function<void()> callback, const DebugLocation& location) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer[%p] Scheduling callback [%s:%d]", this,
            location.file(), location.line());
  }
  MutexLock lock(&mu_);
  if (!running_) {
    running_ = true;
    GPR_ASSERT(processing_.empty());
    processing_.emplace_back(std::move(callback), location);
    event_engine_->Run(this);
  } else {
    incoming_.emplace_back(std::move(callback), location);
  }
}

void WorkSerializer::DispatchingWorkSerializer::Run() {
  ApplicationCallbackExecCtx app_exec_ctx;
  ExecCtx exec_ctx;
  auto& cb = processing_.back();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_work_serializer_trace)) {
    gpr_log(GPR_INFO, "WorkSerializer[%p] Executing callback [%s:%d]", this,
            cb.location.file(), cb.location.line());
  }
  SetCurrentThread();
  cb.callback();
  processing_.pop_back();
  ClearCurrentThread();
  if (processing_.empty()) {
    processing_.shrink_to_fit();
    ReleasableMutexLock lock(&mu_);
    processing_.swap(incoming_);
    if (processing_.empty()) {
      running_ = false;
      if (orphaned_) {
        lock.Release();
        delete this;
      }
      return;
    }
    lock.Release();
    std::reverse(processing_.begin(), processing_.end());
  }
  event_engine_->Run(this);
}

//
// WorkSerializer
//

WorkSerializer::WorkSerializer(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : impl_(IsWorkSerializerDispatchEnabled()
                ? OrphanablePtr<WorkSerializerImpl>(
                      MakeOrphanable<DispatchingWorkSerializer>(
                          std::move(event_engine)))
                : OrphanablePtr<WorkSerializerImpl>(
                      MakeOrphanable<LegacyWorkSerializer>())) {}

WorkSerializer::~WorkSerializer() = default;

void WorkSerializer::Run(std::function<void()> callback,
                         const DebugLocation& location) {
  impl_->Run(std::move(callback), location);
}

void WorkSerializer::Schedule(std::function<void()> callback,
                              const DebugLocation& location) {
  impl_->Schedule(std::move(callback), location);
}

void WorkSerializer::DrainQueue() { impl_->DrainQueue(); }

#ifndef NDEBUG
bool WorkSerializer::RunningInWorkSerializer() const {
  return impl_->RunningInWorkSerializer();
}
#endif

}  // namespace grpc_core
