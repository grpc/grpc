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

#ifndef GRPC_CORE_LIB_RESOURCE_QUOTA_MEMORY_QUOTA_H
#define GRPC_CORE_LIB_RESOURCE_QUOTA_MEMORY_QUOTA_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/memory_request.h>
#include <grpc/support/log.h>

#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/periodic_update.h"

namespace grpc_core {

class BasicMemoryQuota;
class MemoryQuota;

using grpc_event_engine::experimental::MemoryRequest;

// Pull in impl under a different name to keep the gRPC/EventEngine separation
// clear.
using EventEngineMemoryAllocatorImpl =
    grpc_event_engine::experimental::internal::MemoryAllocatorImpl;
using grpc_event_engine::experimental::MemoryAllocator;
template <typename T>
using Vector = grpc_event_engine::experimental::Vector<T>;

// Reclamation passes.
// When memory is tight, we start trying to claim some back from memory
// reclaimers. We do this in multiple passes: if there is a less destructive
// operation available, we do that, otherwise we do something more destructive.
enum class ReclamationPass {
  // Non-empty reclamation ought to take index 0, but to simplify API we don't
  // expose that publicly (it's an internal detail), and hence index zero is
  // here unnamed.

  // Benign reclamation is intended for reclamation steps that are not
  // observable outside of gRPC (besides maybe causing an increase in CPU
  // usage).
  // Examples of such reclamation would be resizing buffers to fit the current
  // load needs, rather than whatever was the peak usage requirement.
  kBenign = 1,
  // Idle reclamation is intended for reclamation steps that are observable
  // outside of gRPC, but do not cause application work to be lost.
  // Examples of such reclamation would be dropping channels that are not being
  // used.
  kIdle = 2,
  // Destructive reclamation is our last resort, and is these reclamations are
  // allowed to drop work - such as cancelling in flight requests.
  kDestructive = 3,
};
static constexpr size_t kNumReclamationPasses = 4;

// For each reclamation function run we construct a ReclamationSweep.
// When this object is finally destroyed (it may be moved several times first),
// then that reclamation is complete and we may continue the reclamation loop.
class ReclamationSweep {
 public:
  ReclamationSweep() = default;
  ReclamationSweep(std::shared_ptr<BasicMemoryQuota> memory_quota,
                   uint64_t sweep_token, Waker waker)
      : memory_quota_(std::move(memory_quota)),
        sweep_token_(sweep_token),
        waker_(std::move(waker)) {}
  ~ReclamationSweep();

  ReclamationSweep(const ReclamationSweep&) = delete;
  ReclamationSweep& operator=(const ReclamationSweep&) = delete;
  ReclamationSweep(ReclamationSweep&&) = default;
  ReclamationSweep& operator=(ReclamationSweep&&) = default;

  // Has enough work been done that we would not be called upon again
  // immediately to do reclamation work if we stopped and requeued. Reclaimers
  // with a variable amount of work to do can use this to ascertain when they
  // can stop more efficiently than going through the reclaimer queue once per
  // work item.
  bool IsSufficient() const;

  // Explicit finish for users that wish to write it.
  // Just destroying the object is enough, but sometimes the additional
  // explicitness is warranted.
  void Finish() {
    [](ReclamationSweep) {}(std::move(*this));
  }

 private:
  std::shared_ptr<BasicMemoryQuota> memory_quota_;
  uint64_t sweep_token_;
  Waker waker_;
};

class ReclaimerQueue {
 private:
  struct QueuedNode;
  struct State;

 public:
  class Handle : public InternallyRefCounted<Handle> {
   public:
    Handle() = default;
    template <typename F>
    explicit Handle(F reclaimer, std::shared_ptr<State> state)
        : sweep_(new SweepFn<F>(std::move(reclaimer), std::move(state))) {}
    ~Handle() override {
      GPR_DEBUG_ASSERT(sweep_.load(std::memory_order_relaxed) == nullptr);
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    void Orphan() final;
    void Run(ReclamationSweep reclamation_sweep);
    bool Requeue(ReclaimerQueue* new_queue);

   private:
    friend class ReclaimerQueue;
    using InternallyRefCounted<Handle>::Ref;

    class Sweep {
     public:
      virtual void RunAndDelete(absl::optional<ReclamationSweep> sweep) = 0;

     protected:
      explicit Sweep(std::shared_ptr<State> state) : state_(std::move(state)) {}
      ~Sweep() = default;
      void MarkCancelled();

     private:
      std::shared_ptr<State> state_;
    };

    template <typename F>
    class SweepFn final : public Sweep {
     public:
      explicit SweepFn(F&& f, std::shared_ptr<State> state)
          : Sweep(std::move(state)), f_(std::move(f)) {}
      void RunAndDelete(absl::optional<ReclamationSweep> sweep) override {
        if (!sweep.has_value()) MarkCancelled();
        f_(std::move(sweep));
        delete this;
      }

     private:
      F f_;
    };

    std::atomic<Sweep*> sweep_{nullptr};
  };

  ReclaimerQueue();
  ~ReclaimerQueue();

  ReclaimerQueue(const ReclaimerQueue&) = delete;
  ReclaimerQueue& operator=(const ReclaimerQueue&) = delete;

  // Insert a new element at the back of the queue.
  // If there is already an element from allocator at *index, then it is
  // replaced with the new reclaimer and *index is unchanged. If there is not,
  // then *index is set to the index of the newly queued entry.
  // Associates the reclamation function with an allocator, and keeps that
  // allocator alive, so that we can use the pointer as an ABA guard.
  template <typename F>
  GRPC_MUST_USE_RESULT OrphanablePtr<Handle> Insert(F reclaimer) {
    auto p = MakeOrphanable<Handle>(std::move(reclaimer), state_);
    Enqueue(p->Ref());
    return p;
  }

  // Poll to see if an entry is available: returns Pending if not, or the
  // removed reclamation function if so.
  Poll<RefCountedPtr<Handle>> PollNext();

  // This callable is the promise backing Next - it resolves when there is an
  // entry available. This really just redirects to calling PollNext().
  class NextPromise {
   public:
    explicit NextPromise(ReclaimerQueue* queue) : queue_(queue) {}
    Poll<RefCountedPtr<Handle>> operator()() { return queue_->PollNext(); }

   private:
    // Borrowed ReclaimerQueue backing this promise.
    ReclaimerQueue* queue_;
  };
  GRPC_MUST_USE_RESULT NextPromise Next() { return NextPromise(this); }

 private:
  void Enqueue(RefCountedPtr<Handle> handle);

  std::shared_ptr<State> state_;
};

namespace memory_quota_detail {
// Controller: tries to adjust a control variable up or down to get memory
// pressure to some target. We use the control variable to size buffers
// throughout the stack.
class PressureController {
 public:
  PressureController(uint8_t max_ticks_same, uint8_t max_reduction_per_tick)
      : max_ticks_same_(max_ticks_same),
        max_reduction_per_tick_(max_reduction_per_tick) {}
  // Update the controller, returns the new control value.
  double Update(double error);
  // Textual representation of the controller.
  std::string DebugString() const;

 private:
  // How many update periods have we reached the same decision in a row?
  // Too many and we should start expanding the search space since we're not
  // being agressive enough.
  uint8_t ticks_same_ = 0;
  // Maximum number of ticks with the same value until we start expanding the
  // control space.
  const uint8_t max_ticks_same_;
  // Maximum amount to reduce the reporting value per iteration (in tenths of a
  // percentile).
  const uint8_t max_reduction_per_tick_;
  // Was the last error indicating a too low pressure (or if false,
  // a too high pressure).
  bool last_was_low_ = true;
  // Current minimum value to report.
  double min_ = 0.0;
  // Current maximum value to report.
  // Set so that the first change over will choose 1.0 for max.
  double max_ = 2.0;
  // Last control value reported.
  double last_control_ = 0.0;
};

// Utility to track memory pressure.
// Tries to be conservative (returns a higher pressure than there may actually
// be) but to be eventually accurate.
class PressureTracker {
 public:
  double AddSampleAndGetControlValue(double sample);

 private:
  std::atomic<double> max_this_round_{0.0};
  std::atomic<double> report_{0.0};
  PeriodicUpdate update_{Duration::Seconds(1)};
  PressureController controller_{100, 3};
};
}  // namespace memory_quota_detail

class BasicMemoryQuota final
    : public std::enable_shared_from_this<BasicMemoryQuota> {
 public:
  // Data about current memory pressure.
  struct PressureInfo {
    // The current instantaneously measured memory pressure.
    double instantaneous_pressure;
    // A control value that can be used to scale buffer sizes up or down to
    // adjust memory pressure to our target set point.
    double pressure_control_value;
    // Maximum recommended individual allocation size.
    size_t max_recommended_allocation_size;
  };

  explicit BasicMemoryQuota(std::string name) : name_(std::move(name)) {}

  // Start the reclamation activity.
  void Start();
  // Stop the reclamation activity.
  // Until reclamation is stopped, it's possible that circular references to the
  // BasicMemoryQuota remain. i.e. to guarantee deletion, a singular owning
  // object should call BasicMemoryQuota::Stop().
  void Stop();

  // Resize the quota to new_size.
  void SetSize(size_t new_size);
  // Forcefully take some memory from the quota, potentially entering
  // overcommit.
  void Take(size_t amount);
  // Finish reclamation pass.
  void FinishReclamation(uint64_t token, Waker waker);
  // Return some memory to the quota.
  void Return(size_t amount);
  // Instantaneous memory pressure approximation.
  PressureInfo GetPressureInfo();
  // Get a reclamation queue
  ReclaimerQueue* reclaimer_queue(size_t i) { return &reclaimers_[i]; }

  // The name of this quota
  absl::string_view name() const { return name_; }

 private:
  friend class ReclamationSweep;
  class WaitForSweepPromise;

  static constexpr intptr_t kInitialSize = std::numeric_limits<intptr_t>::max();

  // The amount of memory that's free in this quota.
  // We use intptr_t as a reasonable proxy for ssize_t that's portable.
  // We allow arbitrary overcommit and so this must allow negative values.
  std::atomic<intptr_t> free_bytes_{kInitialSize};
  // The total number of bytes in this quota.
  std::atomic<size_t> quota_size_{kInitialSize};

  // Reclaimer queues.
  ReclaimerQueue reclaimers_[kNumReclamationPasses];
  // The reclaimer activity consumes reclaimers whenever we are in overcommit to
  // try and get back under memory limits.
  ActivityPtr reclaimer_activity_;
  // Each time we do a reclamation sweep, we increment this counter and give it
  // to the sweep in question. In this way, should we choose to cancel a sweep
  // we can do so and not get confused when the sweep reports back that it's
  // completed.
  // We also increment this counter on completion of a sweep, as an indicator
  // that the wait has ended.
  std::atomic<uint64_t> reclamation_counter_{0};
  // Memory pressure smoothing
  memory_quota_detail::PressureTracker pressure_tracker_;
  // The name of this quota - used for debugging/tracing/etc..
  std::string name_;
};

// MemoryAllocatorImpl grants the owner the ability to allocate memory from an
// underlying resource quota.
class GrpcMemoryAllocatorImpl final : public EventEngineMemoryAllocatorImpl {
 public:
  explicit GrpcMemoryAllocatorImpl(
      std::shared_ptr<BasicMemoryQuota> memory_quota, std::string name);
  ~GrpcMemoryAllocatorImpl() override;

  // Reserve bytes from the quota.
  // If we enter overcommit, reclamation will begin concurrently.
  // Returns the number of bytes reserved.
  size_t Reserve(MemoryRequest request) override;

  // Release some bytes that were previously reserved.
  void Release(size_t n) override {
    // Add the released memory to our free bytes counter... if this increases
    // from  0 to non-zero, then we have more to do, otherwise, we're actually
    // done.
    size_t prev_free = free_bytes_.fetch_add(n, std::memory_order_release);
    if ((!IsUnconstrainedMaxQuotaBufferSizeEnabled() &&
         prev_free + n > kMaxQuotaBufferSize) ||
        (IsPeriodicResourceQuotaReclamationEnabled() &&
         donate_back_.Tick([](Duration) {}))) {
      // Try to immediately return some free'ed memory back to the total quota.
      MaybeDonateBack();
    }
    if (prev_free != 0) return;
    MaybeRegisterReclaimer();
  }

  // Post a reclamation function.
  template <typename F>
  void PostReclaimer(ReclamationPass pass, F fn) {
    MutexLock lock(&reclaimer_mu_);
    GPR_ASSERT(!shutdown_);
    InsertReclaimer(static_cast<size_t>(pass), std::move(fn));
  }

  // Shutdown the allocator.
  void Shutdown() override;

  // Read the instantaneous memory pressure
  BasicMemoryQuota::PressureInfo GetPressureInfo() const {
    return memory_quota_->GetPressureInfo();
  }

  // Name of this allocator
  absl::string_view name() const { return name_; }

 private:
  static constexpr size_t kMaxQuotaBufferSize = 1024 * 1024;
  // Primitive reservation function.
  absl::optional<size_t> TryReserve(MemoryRequest request) GRPC_MUST_USE_RESULT;
  // This function may be invoked during a memory release operation.
  // It will try to return half of our free pool to the quota.
  void MaybeDonateBack();
  // Replenish bytes from the quota, without blocking, possibly entering
  // overcommit.
  void Replenish();
  // If we have not already, register a reclamation function against the quota
  // to sweep any free memory back to that quota.
  void MaybeRegisterReclaimer() ABSL_LOCKS_EXCLUDED(reclaimer_mu_);
  template <typename F>
  void InsertReclaimer(size_t pass, F fn)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(reclaimer_mu_) {
    reclamation_handles_[pass] =
        memory_quota_->reclaimer_queue(pass)->Insert(std::move(fn));
  }

  // Backing resource quota.
  const std::shared_ptr<BasicMemoryQuota> memory_quota_;
  // Amount of memory this allocator has cached for its own use: to avoid quota
  // contention, each MemoryAllocator can keep some memory in addition to what
  // it is immediately using, and the quota can pull it back under memory
  // pressure.
  std::atomic<size_t> free_bytes_{0};
  // Amount of memory taken from the quota by this allocator.
  std::atomic<size_t> taken_bytes_{sizeof(GrpcMemoryAllocatorImpl)};
  std::atomic<bool> registered_reclaimer_{false};
  // We try to donate back some memory periodically to the central quota.
  PeriodicUpdate donate_back_{Duration::Seconds(10)};
  Mutex reclaimer_mu_;
  bool shutdown_ ABSL_GUARDED_BY(reclaimer_mu_) = false;
  // Indices into the various reclaimer queues, used so that we can cancel
  // reclamation should we shutdown or get rebound.
  OrphanablePtr<ReclaimerQueue::Handle>
      reclamation_handles_[kNumReclamationPasses] ABSL_GUARDED_BY(
          reclaimer_mu_);
  // Name of this allocator.
  std::string name_;
};

// MemoryOwner is an enhanced MemoryAllocator that can also reclaim memory, and
// be rebound to a different memory quota.
// Different modules should not share a MemoryOwner between themselves, instead
// each module that requires a MemoryOwner should create one from a resource
// quota. This is because the MemoryOwner reclaimers are tied to the
// MemoryOwner's lifetime, and are not queryable, so passing a MemoryOwner to a
// new owning module means that module cannot reason about which reclaimers are
// active, nor what they might do.
class MemoryOwner final : public MemoryAllocator {
 public:
  MemoryOwner() = default;

  explicit MemoryOwner(std::shared_ptr<GrpcMemoryAllocatorImpl> allocator)
      : MemoryAllocator(std::move(allocator)) {}

  // Post a reclaimer for some reclamation pass.
  template <typename F>
  void PostReclaimer(ReclamationPass pass, F fn) {
    impl()->PostReclaimer(pass, std::move(fn));
  }

  // Instantaneous memory pressure in the underlying quota.
  BasicMemoryQuota::PressureInfo GetPressureInfo() const {
    return impl()->GetPressureInfo();
  }

  template <typename T, typename... Args>
  OrphanablePtr<T> MakeOrphanable(Args&&... args) {
    return OrphanablePtr<T>(New<T>(std::forward<Args>(args)...));
  }

  // Name of this object
  absl::string_view name() const { return impl()->name(); }

  // Is this object valid (ie has not been moved out of or reset)
  bool is_valid() const { return impl() != nullptr; }

 private:
  const GrpcMemoryAllocatorImpl* impl() const {
    return static_cast<const GrpcMemoryAllocatorImpl*>(get_internal_impl_ptr());
  }

  GrpcMemoryAllocatorImpl* impl() {
    return static_cast<GrpcMemoryAllocatorImpl*>(get_internal_impl_ptr());
  }
};

// MemoryQuota tracks the amount of memory available as part of a ResourceQuota.
class MemoryQuota final
    : public grpc_event_engine::experimental::MemoryAllocatorFactory {
 public:
  explicit MemoryQuota(std::string name)
      : memory_quota_(std::make_shared<BasicMemoryQuota>(std::move(name))) {
    memory_quota_->Start();
  }
  ~MemoryQuota() override {
    if (memory_quota_ != nullptr) memory_quota_->Stop();
  }

  MemoryQuota(const MemoryQuota&) = delete;
  MemoryQuota& operator=(const MemoryQuota&) = delete;
  MemoryQuota(MemoryQuota&&) = default;
  MemoryQuota& operator=(MemoryQuota&&) = default;

  MemoryAllocator CreateMemoryAllocator(absl::string_view name) override;
  MemoryOwner CreateMemoryOwner(absl::string_view name);

  // Resize the quota to new_size.
  void SetSize(size_t new_size) { memory_quota_->SetSize(new_size); }

  // Return true if the instantaneous memory pressure is high.
  bool IsMemoryPressureHigh() const {
    static constexpr double kMemoryPressureHighThreshold = 1.0;
    return memory_quota_->GetPressureInfo().instantaneous_pressure >
           kMemoryPressureHighThreshold;
  }

 private:
  friend class MemoryOwner;
  std::shared_ptr<BasicMemoryQuota> memory_quota_;
};

using MemoryQuotaRefPtr = std::shared_ptr<MemoryQuota>;
inline MemoryQuotaRefPtr MakeMemoryQuota(std::string name) {
  return std::make_shared<MemoryQuota>(std::move(name));
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_RESOURCE_QUOTA_MEMORY_QUOTA_H
