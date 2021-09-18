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
#include <queue>
#include <vector>

#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

class Reclaimer;
class MemoryAllocator;
class MemoryQuota;

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

// Reservation request - how much memory do we want to allocate?
class MemoryRequest {
 public:
  // Request a fixed amount of memory.
  // NOLINTNEXTLINE(google-explicit-constructor)
  MemoryRequest(size_t n) : min_(n), max_(n) {}
  // Request a range of memory.
  MemoryRequest(size_t min, size_t max) : min_(std::min(min, max)), max_(max) {}

  // Increase the size by amount
  MemoryRequest Increase(size_t amount) const;

  size_t min() const { return min_; }
  size_t max() const { return max_; }

 private:
  size_t min_;
  size_t max_;
};

// For each reclamation function run we construct a ReclamationSweep.
// When this object is finally destroyed (it may be moved several times first),
// then that reclamation is complete and we may continue the reclamation loop.
class ReclamationSweep {
 public:
  ReclamationSweep(WeakRefCountedPtr<MemoryQuota> memory_quota,
                   uint64_t sweep_token)
      : memory_quota_(std::move(memory_quota)), sweep_token_(sweep_token) {}
  ~ReclamationSweep();

  ReclamationSweep(const ReclamationSweep&) = delete;
  ReclamationSweep& operator=(const ReclamationSweep&) = delete;
  ReclamationSweep(ReclamationSweep&&) = default;
  ReclamationSweep& operator=(ReclamationSweep&&) = default;

  bool IsSufficient() const;

 private:
  WeakRefCountedPtr<MemoryQuota> memory_quota_;
  uint64_t sweep_token_;
};

using ReclamationFunction = std::function<void(ReclamationSweep)>;

class ReclaimerQueue {
 public:
  using Index = size_t;

  static constexpr Index kInvalidIndex = std::numeric_limits<Index>::max();

  Index Insert(RefCountedPtr<MemoryAllocator> allocator,
               ReclamationFunction reclaimer) ABSL_LOCKS_EXCLUDED(mu_);
  ReclamationFunction Cancel(Index index, MemoryAllocator* allocator)
      ABSL_LOCKS_EXCLUDED(mu_);
  Poll<ReclamationFunction> PollNext() ABSL_LOCKS_EXCLUDED(mu_);

  class NextPromise {
   public:
    explicit NextPromise(ReclaimerQueue* queue) : queue_(queue) {}
    Poll<ReclamationFunction> operator()() { return queue_->PollNext(); }

   private:
    ReclaimerQueue* queue_;
  };
  NextPromise Next() { return NextPromise(this); }

 private:
  Mutex mu_;
  struct Entry {
    Entry(RefCountedPtr<MemoryAllocator> allocator,
          ReclamationFunction reclaimer)
        : allocator(allocator), reclaimer(reclaimer) {}
    RefCountedPtr<MemoryAllocator> allocator;
    ReclamationFunction reclaimer;
  };
  std::vector<Entry> entries_ ABSL_GUARDED_BY(mu_);
  std::vector<size_t> free_entries_ ABSL_GUARDED_BY(mu_);
  std::queue<Index> queue_ ABSL_GUARDED_BY(mu_);
  Waker waker_ ABSL_GUARDED_BY(mu_);
};

// MemoryAllocator grants the owner the ability to allocate memory from an
// underlying resource quota.
class MemoryAllocator final : public InternallyRefCounted<MemoryAllocator> {
 public:
  explicit MemoryAllocator(RefCountedPtr<MemoryQuota> memory_quota);
  ~MemoryAllocator() override;

  void Orphan() override;

  // Rebind -  Swaps the underlying quota for this allocator, taking care to
  // make sure memory allocated is moved to allocations against the new quota.
  void Rebind(RefCountedPtr<MemoryQuota> memory_quota)
      ABSL_LOCKS_EXCLUDED(memory_quota_mu_);

  // Reserve bytes from the quota.
  // If we enter overcommit, reclamation will begin concurrently.
  // Returns the number of bytes reserved.
  size_t Reserve(MemoryRequest request) ABSL_LOCKS_EXCLUDED(memory_quota_mu_);

  // Release some bytes that were previously reserved.
  void Release(size_t n) ABSL_LOCKS_EXCLUDED(memory_quota_mu_) {
    // Add the released memory to our free bytes counter... if this increases
    // from  0 to non-zero, then we have more to do, otherwise, we're actually
    // done.
    if (free_bytes_.fetch_add(n, std::memory_order_release) != 0) return;
    MaybeRegisterReclaimer();
  }

  // Post a reclaimer for some reclamation pass.
  void PostReclaimer(ReclamationPass pass,
                     std::function<void(ReclamationSweep)>);

  // Allocate a new object of type T, with constructor arguments.
  // The returned type is wrapped, and upon destruction the reserved memory
  // will be released to the allocator automatically. As such, T must have a
  // virtual destructor so we can insert the necessary hook.
  template <typename T, typename... Args>
  absl::enable_if_t<std::has_virtual_destructor<T>::value, T*> New(
      Args&&... args) ABSL_LOCKS_EXCLUDED(memory_quota_mu_) {
    // Wrap T such that when it's destroyed, we can release memory back to the
    // allocator.
    class Wrapper final : public T {
     public:
      explicit Wrapper(RefCountedPtr<MemoryAllocator> allocator, Args&&... args)
          : T(std::forward<Args>(args)...), allocator_(std::move(allocator)) {}
      ~Wrapper() override { allocator_->Release(sizeof(*this)); }

     private:
      const RefCountedPtr<MemoryAllocator> allocator_;
    };
    Reserve(sizeof(Wrapper));
    return new Wrapper(Ref(), std::forward<Args>(args)...);
  }

  // Construct a unique ptr immediately.
  template <typename T, typename... Args>
  std::unique_ptr<T> MakeUnique(Args&&... args)
      ABSL_LOCKS_EXCLUDED(memory_quota_mu_) {
    return std::unique_ptr<T>(New<T>(std::forward<Args>(args)...));
  }

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
  Mutex memory_quota_mu_;
  // Backing resource quota.
  RefCountedPtr<MemoryQuota> memory_quota_ ABSL_GUARDED_BY(memory_quota_mu_);
  // Amount of memory taken from the quota by this allocator.
  size_t taken_bytes_ ABSL_GUARDED_BY(memory_quota_mu_) = 0;
  // Indices into the various reclaimer queues, used so that we can cancel
  // reclamation should we shutdown or get rebound.
  ReclaimerQueue::Index
      reclamation_indices_[kNumReclamationPasses] ABSL_GUARDED_BY(
          memory_quota_mu_) = {ReclaimerQueue::kInvalidIndex};
};

class AtomicBarrier {
 public:
  AtomicBarrier() = default;
  AtomicBarrier(const AtomicBarrier&) = delete;
  AtomicBarrier& operator=(const AtomicBarrier&) = delete;

  class WaitPromise {
   public:
    WaitPromise(AtomicBarrier* barrier, uint64_t token)
        : barrier_(barrier), token_(token) {}

    struct Empty {};
    Poll<Empty> operator()();

   private:
    AtomicBarrier* barrier_;
    uint64_t token_;
  };

  uint64_t NewToken();
  WaitPromise Wait(uint64_t token) { return WaitPromise(this, token); }
  void Notify(uint64_t token);

 private:
  std::atomic<uint64_t> counter_{0};
  Waker waker_;
};

// MemoryQuota tracks the amount of memory available as part of a ResourceQuota.
class MemoryQuota final : public DualRefCounted<MemoryQuota> {
 public:
  MemoryQuota();

  OrphanablePtr<MemoryAllocator> MakeMemoryAllocator() {
    return MakeOrphanable<MemoryAllocator>(Ref());
  }

  // Resize the quota to new_size.
  void SetSize(size_t new_size);

 private:
  friend class MemoryAllocator;
  friend class ReclamationSweep;

  void Orphan() override;

  // Forcefully take some memory from the quota, potentially entering
  // overcommit.
  void Take(size_t amount);
  // Finish reclamation pass.
  void FinishReclamation(uint64_t token);
  // Return some memory to the quota.
  void Return(size_t amount);
  // Instantaneous memory pressure approximation.
  size_t InstantaneousPressure() const {
    double free = free_bytes_.load();
    if (free < 0) free = 0;
    double size = quota_size_.load();
    if (size < 1) return 1.0;
    double pressure = (size - free) / size;
    if (pressure < 0.0) pressure = 0.0;
    if (pressure > 1.0) pressure = 1.0;
    return pressure;
  }

  static constexpr ssize_t kInitialSize = std::numeric_limits<ssize_t>::max();

  // The amount of memory that's free in this quota.
  std::atomic<ssize_t> free_bytes_{kInitialSize};

  // Reclaimer queues.
  ReclaimerQueue reclaimers_[kNumReclamationPasses];
  // The reclaimer activity consumes reclaimers whenever we are in overcommit to
  // try and get back under memory limits.
  ActivityPtr reclaimer_activity_;
  // A barrier that blocks the reclaimer activity whilst a reclamation is in
  // progress.
  AtomicBarrier barrier_;
  // The total number of bytes in this quota.
  std::atomic<size_t> quota_size_{kInitialSize};
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_RESOURCE_QUOTA_MEMORY_QUOTA_H
