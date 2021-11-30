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

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <queue>
#include <vector>

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/slice.h>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"

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
                   uint64_t sweep_token)
      : memory_quota_(std::move(memory_quota)), sweep_token_(sweep_token) {}
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
};

using ReclamationFunction =
    std::function<void(absl::optional<ReclamationSweep>)>;

class ReclaimerQueue {
 public:
  using Index = size_t;

  // An invalid index usable as an empty value.
  // This value will not be returned from Insert ever.
  static constexpr Index kInvalidIndex = std::numeric_limits<Index>::max();

  // Insert a new element at the back of the queue.
  // If there is already an element from allocator at *index, then it is
  // replaced with the new reclaimer and *index is unchanged. If there is not,
  // then *index is set to the index of the newly queued entry.
  // Associates the reclamation function with an allocator, and keeps that
  // allocator alive, so that we can use the pointer as an ABA guard.
  void Insert(std::shared_ptr<EventEngineMemoryAllocatorImpl> allocator,
              ReclamationFunction reclaimer, Index* index)
      ABSL_LOCKS_EXCLUDED(mu_);
  // Cancel a reclamation function - returns the function if cancelled
  // successfully, or nullptr if the reclamation was already begun and could not
  // be cancelled. allocator must be the same as was passed to Insert.
  ReclamationFunction Cancel(Index index,
                             EventEngineMemoryAllocatorImpl* allocator)
      ABSL_LOCKS_EXCLUDED(mu_);
  // Poll to see if an entry is available: returns Pending if not, or the
  // removed reclamation function if so.
  Poll<ReclamationFunction> PollNext() ABSL_LOCKS_EXCLUDED(mu_);

  // This callable is the promise backing Next - it resolves when there is an
  // entry available. This really just redirects to calling PollNext().
  class NextPromise {
   public:
    explicit NextPromise(ReclaimerQueue* queue) : queue_(queue) {}
    Poll<ReclamationFunction> operator()() { return queue_->PollNext(); }

   private:
    // Borrowed ReclaimerQueue backing this promise.
    ReclaimerQueue* queue_;
  };
  NextPromise Next() { return NextPromise(this); }

 private:
  // One entry in the reclaimer queue
  struct Entry {
    Entry(std::shared_ptr<EventEngineMemoryAllocatorImpl> allocator,
          ReclamationFunction reclaimer)
        : allocator(std::move(allocator)), reclaimer(reclaimer) {}
    // The allocator we'd be reclaiming for.
    std::shared_ptr<EventEngineMemoryAllocatorImpl> allocator;
    // The reclamation function to call.
    ReclamationFunction reclaimer;
  };
  // Guarding mutex.
  Mutex mu_;
  // Entries in the queue (or empty entries waiting to be queued).
  // We actually queue indices into this vector - and do this so that
  // we can use the memory allocator pointer as an ABA protection.
  std::vector<Entry> entries_ ABSL_GUARDED_BY(mu_);
  // Which entries in entries_ are not allocated right now.
  std::vector<size_t> free_entries_ ABSL_GUARDED_BY(mu_);
  // Allocated entries waiting to be consumed.
  std::queue<Index> queue_ ABSL_GUARDED_BY(mu_);
  // Potentially one activity can be waiting for new entries on the queue.
  Waker waker_ ABSL_GUARDED_BY(mu_);
};

class BasicMemoryQuota final
    : public std::enable_shared_from_this<BasicMemoryQuota> {
 public:
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
  void FinishReclamation(uint64_t token);
  // Return some memory to the quota.
  void Return(size_t amount);
  // Instantaneous memory pressure approximation.
  std::pair<double, size_t>
  InstantaneousPressureAndMaxRecommendedAllocationSize() const;
  // Cancel a reclaimer
  ReclamationFunction CancelReclaimer(
      size_t reclaimer, typename ReclaimerQueue::Index index,
      EventEngineMemoryAllocatorImpl* allocator) {
    return reclaimers_[reclaimer].Cancel(index, allocator);
  }
  // Insert a reclaimer
  void InsertReclaimer(
      size_t reclaimer,
      std::shared_ptr<EventEngineMemoryAllocatorImpl> allocator,
      ReclamationFunction fn, ReclaimerQueue::Index* index) {
    reclaimers_[reclaimer].Insert(std::move(allocator), std::move(fn), index);
  }

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

  // Rebind - Swaps the underlying quota for this allocator, taking care to
  // make sure memory allocated is moved to allocations against the new quota.
  void Rebind(std::shared_ptr<BasicMemoryQuota> memory_quota)
      ABSL_LOCKS_EXCLUDED(memory_quota_mu_);

  // Reserve bytes from the quota.
  // If we enter overcommit, reclamation will begin concurrently.
  // Returns the number of bytes reserved.
  size_t Reserve(MemoryRequest request) override;

  // Release some bytes that were previously reserved.
  void Release(size_t n) override {
    // Add the released memory to our free bytes counter... if this increases
    // from  0 to non-zero, then we have more to do, otherwise, we're actually
    // done.
    if (free_bytes_.fetch_add(n, std::memory_order_release) != 0) return;
    MaybeRegisterReclaimer();
  }

  // Post a reclamation function.
  void PostReclaimer(ReclamationPass pass, ReclamationFunction fn);

  // Shutdown the allocator.
  void Shutdown() override;

  // Read the instantaneous memory pressure
  double InstantaneousPressure() const {
    MutexLock lock(&memory_quota_mu_);
    return memory_quota_->InstantaneousPressureAndMaxRecommendedAllocationSize()
        .first;
  }

  // Name of this allocator
  absl::string_view name() const { return name_; }

 private:
  // Primitive reservation function.
  absl::optional<size_t> TryReserve(MemoryRequest request) GRPC_MUST_USE_RESULT;
  // Replenish bytes from the quota, without blocking, possibly entering
  // overcommit.
  void Replenish() ABSL_LOCKS_EXCLUDED(memory_quota_mu_);
  // If we have not already, register a reclamation function against the quota
  // to sweep any free memory back to that quota.
  void MaybeRegisterReclaimer() ABSL_LOCKS_EXCLUDED(memory_quota_mu_);
  void MaybeRegisterReclaimerLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(memory_quota_mu_);

  // Amount of memory this allocator has cached for its own use: to avoid quota
  // contention, each MemoryAllocator can keep some memory in addition to what
  // it is immediately using, and the quota can pull it back under memory
  // pressure.
  std::atomic<size_t> free_bytes_{0};
  // Mutex guarding the backing resource quota.
  mutable Mutex memory_quota_mu_;
  // Backing resource quota.
  std::shared_ptr<BasicMemoryQuota> memory_quota_
      ABSL_GUARDED_BY(memory_quota_mu_);
  // Amount of memory taken from the quota by this allocator.
  size_t taken_bytes_ ABSL_GUARDED_BY(memory_quota_mu_) =
      sizeof(GrpcMemoryAllocatorImpl);
  bool shutdown_ ABSL_GUARDED_BY(memory_quota_mu_) = false;
  // Indices into the various reclaimer queues, used so that we can cancel
  // reclamation should we shutdown or get rebound.
  ReclaimerQueue::Index
      reclamation_indices_[kNumReclamationPasses] ABSL_GUARDED_BY(
          memory_quota_mu_) = {
          ReclaimerQueue::kInvalidIndex, ReclaimerQueue::kInvalidIndex,
          ReclaimerQueue::kInvalidIndex, ReclaimerQueue::kInvalidIndex};
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
  void PostReclaimer(ReclamationPass pass, ReclamationFunction fn) {
    impl()->PostReclaimer(pass, std::move(fn));
  }

  // Rebind to a different quota.
  void Rebind(MemoryQuota* quota);

  // Instantaneous memory pressure in the underlying quota.
  double InstantaneousPressure() const {
    return impl()->InstantaneousPressure();
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
    static constexpr double kMemoryPressureHighThreshold = 0.9;
    return memory_quota_->InstantaneousPressureAndMaxRecommendedAllocationSize()
               .first > kMemoryPressureHighThreshold;
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
