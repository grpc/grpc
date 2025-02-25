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

#include "src/core/util/work_serializer.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>
#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/latent_see.h"
#include "src/core/util/mpscq.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/sync.h"

namespace grpc_core {

//
// WorkSerializer::WorkSerializerImpl
//

// Executes callbacks one at a time on EventEngine.
// One at a time guarantees that fixed size thread pools in EventEngine
// implementations are not starved of threads by long running work
// serializers. We implement EventEngine::Closure directly to avoid allocating
// once per callback in the queue when scheduling.
class WorkSerializer::WorkSerializerImpl
    : public Orphanable,
      public grpc_event_engine::experimental::EventEngine::Closure {
 public:
  explicit WorkSerializerImpl(
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine)
      : event_engine_(std::move(event_engine)) {}
  void Run(absl::AnyInvocable<void()> callback, DebugLocation location);
  void Run() override;
  void Orphan() override;

#ifndef NDEBUG
  bool RunningInWorkSerializer() const {
    return running_work_serializer_ == this;
  }
#endif
 private:
  // Wrapper to capture DebugLocation for the callback.
  struct CallbackWrapper {
    CallbackWrapper(absl::AnyInvocable<void()>&& cb, const DebugLocation& loc)
        : callback(std::forward<absl::AnyInvocable<void()>>(cb)),
          location(loc) {}
    absl::AnyInvocable<void()> callback;
    // GPR_NO_UNIQUE_ADDRESS means this is 0 sized in release builds.
    GPR_NO_UNIQUE_ADDRESS DebugLocation location;
  };
  using CallbackVector = absl::InlinedVector<CallbackWrapper, 1>;

  // Refill processing_ from incoming_
  // If processing_ is empty, also update running_ and return false.
  // If additionally orphaned, will also delete this (therefore, it's not safe
  // to touch any member variables if Refill returns false).
  bool Refill();

  // Perform the parts of Refill that need to acquire mu_
  // Returns a tri-state indicating whether we were refilled successfully (=>
  // keep running), or finished, and then if we were orphaned.
  enum class RefillResult { kRefilled, kFinished, kFinishedAndOrphaned };
  RefillResult RefillInner();

#ifndef NDEBUG
  void SetCurrentThread() { running_work_serializer_ = this; }
  void ClearCurrentThread() { running_work_serializer_ = nullptr; }
#else
  void SetCurrentThread() {}
  void ClearCurrentThread() {}
#endif

  // Member variables are roughly sorted to keep processing cache lines
  // separated from incoming cache lines.

  // Callbacks that are currently being processed.
  // Only accessed by: a Run() call going from not-running to running, or a
  // work item being executed in EventEngine -- ie this does not need a mutex
  // because all access is serialized. Stored in reverse execution order so
  // that callbacks can be `pop_back()`'d on completion to free up any
  // resources they hold.
  CallbackVector processing_;
  // EventEngine instance upon which we'll do our work.
  const std::shared_ptr<grpc_event_engine::experimental::EventEngine>
      event_engine_;
  std::chrono::steady_clock::time_point running_start_time_
      ABSL_GUARDED_BY(mu_);
  std::chrono::steady_clock::duration time_running_items_;
  uint64_t items_processed_during_run_;
  // Flags containing run state:
  // - running_ goes from false->true whenever the first callback is scheduled
  //   on an idle WorkSerializer, and transitions back to false after the last
  //   callback scheduled is completed and the WorkSerializer is again idle.
  // - orphaned_ transitions to true once upon Orphan being called.
  // When orphaned_ is true and running_ is false, the WorkSerializerImpl
  // instance is deleted.
  bool running_ ABSL_GUARDED_BY(mu_) = false;
  bool orphaned_ ABSL_GUARDED_BY(mu_) = false;
  Mutex mu_;
  // Queued callbacks. New work items land here, and when processing_ is
  // drained we move this entire queue into processing_ and work on draining
  // it again. In low traffic scenarios this gives two mutex acquisitions per
  // work item, but as load increases we get some natural batching and the
  // rate of mutex acquisitions per work item tends towards 1.
  CallbackVector incoming_ ABSL_GUARDED_BY(mu_);

  GPR_NO_UNIQUE_ADDRESS latent_see::Flow flow_;

#ifndef NDEBUG
  static thread_local WorkSerializerImpl* running_work_serializer_;
#endif
};

#ifndef NDEBUG
thread_local WorkSerializer::WorkSerializerImpl*
    WorkSerializer::WorkSerializerImpl::running_work_serializer_ = nullptr;
#endif

void WorkSerializer::WorkSerializerImpl::Orphan() {
  ReleasableMutexLock lock(&mu_);
  // If we're not running, then we can delete immediately.
  if (!running_) {
    lock.Release();
    delete this;
    return;
  }
  // Otherwise store a flag to delete when we're done.
  orphaned_ = true;
}

// Implementation of WorkSerializerImpl::Run
void WorkSerializer::WorkSerializerImpl::Run(
    absl::AnyInvocable<void()> callback, DebugLocation location) {
  GRPC_TRACE_LOG(work_serializer, INFO)
      << "WorkSerializer[" << this << "] Scheduling callback ["
      << location.file() << ":" << location.line() << "]";
  global_stats().IncrementWorkSerializerItemsEnqueued();
  MutexLock lock(&mu_);
  if (!running_) {
    // If we were previously idle, insert this callback directly into the
    // empty processing_ list and start running.
    running_ = true;
    running_start_time_ = std::chrono::steady_clock::now();
    items_processed_during_run_ = 0;
    time_running_items_ = std::chrono::steady_clock::duration();
    CHECK(processing_.empty());
    processing_.emplace_back(std::move(callback), location);
    event_engine_->Run(this);
  } else {
    // We are already running, so add this callback to the incoming_ list.
    // The work loop will eventually get to it.
    incoming_.emplace_back(std::move(callback), location);
  }
}

// Implementation of EventEngine::Closure::Run - our actual work loop
void WorkSerializer::WorkSerializerImpl::Run() {
  GRPC_LATENT_SEE_PARENT_SCOPE("WorkSerializer::Run");
  flow_.End();
  // TODO(ctiller): remove these when we can deprecate ExecCtx
  ExecCtx exec_ctx;
  // Grab the last element of processing_ - which is the next item in our
  // queue since processing_ is stored in reverse order.
  auto& cb = processing_.back();
  GRPC_TRACE_LOG(work_serializer, INFO)
      << "WorkSerializer[" << this << "] Executing callback ["
      << cb.location.file() << ":" << cb.location.line() << "]";
  // Run the work item.
  const auto start = std::chrono::steady_clock::now();
  SetCurrentThread();
  cb.callback();
  // pop_back here destroys the callback - freeing any resources it might
  // hold. We do so before clearing the current thread in case the callback
  // destructor wants to check that it's in the WorkSerializer too.
  processing_.pop_back();
  ClearCurrentThread();
  global_stats().IncrementWorkSerializerItemsDequeued();
  const auto work_time = std::chrono::steady_clock::now() - start;
  global_stats().IncrementWorkSerializerWorkTimePerItemMs(
      std::chrono::duration_cast<std::chrono::milliseconds>(work_time).count());
  time_running_items_ += work_time;
  ++items_processed_during_run_;
  // Check if we've drained the queue and if so refill it.
  if (processing_.empty() && !Refill()) return;
  // There's still work in processing_, so schedule ourselves again on
  // EventEngine.
  flow_.Begin(GRPC_LATENT_SEE_METADATA("WorkSerializer::Link"));
  event_engine_->Run(this);
}

WorkSerializer::WorkSerializerImpl::RefillResult
WorkSerializer::WorkSerializerImpl::RefillInner() {
  // Recover any memory held by processing_, so that we don't grow forever.
  // Do so before acquiring a lock so we don't cause inadvertent contention.
  processing_.shrink_to_fit();
  MutexLock lock(&mu_);
  // Swap incoming_ into processing_ - effectively lets us release memory
  // (outside the lock) once per iteration for the storage vectors.
  processing_.swap(incoming_);
  // If there were no items, then we've finished running.
  if (processing_.empty()) {
    running_ = false;
    global_stats().IncrementWorkSerializerRunTimeMs(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - running_start_time_)
            .count());
    global_stats().IncrementWorkSerializerWorkTimeMs(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            time_running_items_)
            .count());
    global_stats().IncrementWorkSerializerItemsPerRun(
        items_processed_during_run_);
    // And if we're also orphaned then it's time to delete this object.
    if (orphaned_) {
      return RefillResult::kFinishedAndOrphaned;
    } else {
      return RefillResult::kFinished;
    }
  }
  return RefillResult::kRefilled;
}

bool WorkSerializer::WorkSerializerImpl::Refill() {
  const auto result = RefillInner();
  switch (result) {
    case RefillResult::kRefilled:
      // Reverse processing_ so that we can pop_back() items in the correct
      // order. (note that this is mostly pointer swaps inside the
      // std::function's, so should be relatively cheap even for longer
      // lists). Do so here so we're outside of the RefillInner lock.
      std::reverse(processing_.begin(), processing_.end());
      return true;
    case RefillResult::kFinished:
      return false;
    case RefillResult::kFinishedAndOrphaned:
      // Orphaned and finished - finally delete this object.
      // Here so that the mutex lock in RefillInner is released.
      delete this;
      return false;
  }
  GPR_UNREACHABLE_CODE(return false);
}

//
// WorkSerializer
//

WorkSerializer::WorkSerializer(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine)
    : impl_(OrphanablePtr<WorkSerializerImpl>(
          MakeOrphanable<WorkSerializerImpl>(std::move(event_engine)))) {}

WorkSerializer::~WorkSerializer() = default;

void WorkSerializer::Run(absl::AnyInvocable<void()> callback,
                         DebugLocation location) {
  impl_->Run(std::move(callback), location);
}

#ifndef NDEBUG
bool WorkSerializer::RunningInWorkSerializer() const {
  return impl_->RunningInWorkSerializer();
}
#endif

}  // namespace grpc_core
