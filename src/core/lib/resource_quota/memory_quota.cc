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

#include <grpc/support/port_platform.h>

#include "src/core/lib/resource_quota/memory_quota.h"

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/slice/slice_refcount.h"

namespace grpc_core {

// Maximum number of bytes an allocator will request from a quota in one step.
// Larger allocations than this will require multiple allocation requests.
static constexpr size_t kMaxReplenishBytes = 1024 * 1024;

// Minimum number of bytes an allocator will request from a quota in one step.
static constexpr size_t kMinReplenishBytes = 4096;

//
// Reclaimer
//

ReclamationSweep::~ReclamationSweep() {
  if (memory_quota_ != nullptr) {
    memory_quota_->FinishReclamation(sweep_token_);
  }
}

//
// ReclaimerQueue
//

const ReclaimerQueue::Index ReclaimerQueue::kInvalidIndex;

void ReclaimerQueue::Insert(RefCountedPtr<MemoryAllocator> allocator,
                            ReclamationFunction reclaimer, Index* index) {
  MutexLock lock(&mu_);
  if (*index < entries_.size() && entries_[*index].allocator == allocator) {
    entries_[*index].reclaimer.swap(reclaimer);
    return;
  }
  if (free_entries_.empty()) {
    *index = entries_.size();
    entries_.emplace_back(std::move(allocator), std::move(reclaimer));
  } else {
    *index = free_entries_.back();
    free_entries_.pop_back();
    Entry& entry = entries_[*index];
    entry.allocator = std::move(allocator);
    entry.reclaimer = std::move(reclaimer);
  }
  if (queue_.empty()) waker_.Wakeup();
  queue_.push(*index);
}

ReclamationFunction ReclaimerQueue::Cancel(Index index,
                                           MemoryAllocator* allocator) {
  MutexLock lock(&mu_);
  if (index >= entries_.size()) return nullptr;
  Entry& entry = entries_[index];
  if (entry.allocator.get() != allocator) return {};
  entry.allocator.reset();
  return std::move(entry.reclaimer);
}

Poll<ReclamationFunction> ReclaimerQueue::PollNext() {
  MutexLock lock(&mu_);
  while (true) {
    if (queue_.empty()) {
      waker_ = Activity::current()->MakeNonOwningWaker();
      return Pending{};
    }
    Index index = queue_.front();
    queue_.pop();
    free_entries_.push_back(index);
    Entry& entry = entries_[index];
    if (entry.allocator != nullptr) {
      entry.allocator.reset();
      return std::move(entry.reclaimer);
    }
  }
}

//
// MemoryAllocator
//

MemoryAllocator::MemoryAllocator(RefCountedPtr<MemoryQuota> memory_quota)
    : InternallyRefCounted("MemoryAllocator"), memory_quota_(memory_quota) {
  Reserve(sizeof(MemoryQuota));
}

MemoryAllocator::~MemoryAllocator() {
  GPR_ASSERT(free_bytes_.load(std::memory_order_acquire) +
                 sizeof(MemoryQuota) ==
             taken_bytes_);
  memory_quota_->Return(taken_bytes_);
}

void MemoryAllocator::Orphan() {
  ReclamationFunction old_reclaimers[kNumReclamationPasses];
  {
    MutexLock lock(&memory_quota_mu_);
    for (size_t i = 0; i < kNumReclamationPasses; i++) {
      old_reclaimers[i] =
          memory_quota_->reclaimers_[i].Cancel(reclamation_indices_[i], this);
    }
  }
  InternallyRefCounted<MemoryAllocator>::Unref();
}

size_t MemoryAllocator::Reserve(MemoryRequest request) {
  while (true) {
    // Attempt to reserve memory from our pool.
    auto reservation = TryReserve(request);
    if (reservation.has_value()) return *reservation;
    // If that failed, grab more from the quota and retry.
    Replenish();
  }
}

absl::optional<size_t> MemoryAllocator::TryReserve(MemoryRequest request) {
  // How much memory should we request? (see the scaling below)
  size_t scaled_size_over_min = request.max() - request.min();
  // Scale the request down according to memory pressure if we have that
  // flexibility.
  if (scaled_size_over_min != 0) {
    double pressure;
    {
      MutexLock lock(&memory_quota_mu_);
      pressure = memory_quota_->InstantaneousPressure();
    }
    // Reduce allocation size proportional to the pressure > 80% usage.
    if (pressure > 0.8) {
      scaled_size_over_min =
          std::min(scaled_size_over_min,
                   static_cast<size_t>((request.max() - request.min()) *
                                       (1.0 - pressure) / 0.2));
    }
  }

  // How much do we want to reserve?
  const size_t reserve = request.min() + scaled_size_over_min;
  // See how many bytes are available.
  size_t available = free_bytes_.load(std::memory_order_acquire);
  while (true) {
    // Does the current free pool satisfy the request?
    if (available < reserve) {
      return {};
    }
    // Try to reserve the requested amount.
    // If the amount of free memory changed through this loop, then available
    // will be set to the new value and we'll repeat.
    if (free_bytes_.compare_exchange_weak(available, available - reserve,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
      return reserve;
    }
  }
}

void MemoryAllocator::Replenish() {
  MutexLock lock(&memory_quota_mu_);
  // Attempt a fairly low rate exponential growth request size, bounded between
  // some reasonable limits declared at top of file.
  auto amount = Clamp(taken_bytes_ / 3, kMinReplenishBytes, kMaxReplenishBytes);
  // Take the requested amount from the quota.
  gpr_log(GPR_DEBUG, "%p: take %" PRIdMAX " bytes from quota", this, amount);
  memory_quota_->Take(amount);
  // Record that we've taken it.
  taken_bytes_ += amount;
  // Add the taken amount to the free pool.
  free_bytes_.fetch_add(amount, std::memory_order_acq_rel);
  // See if we can add ourselves as a reclaimer.
  MaybeRegisterReclaimerLocked();
}

void MemoryAllocator::MaybeRegisterReclaimer() {
  MutexLock lock(&memory_quota_mu_);
  MaybeRegisterReclaimerLocked();
}

void MemoryAllocator::MaybeRegisterReclaimerLocked() {
  // If the reclaimer is already registered, then there's nothing to do.
  if (reclamation_indices_[0] != ReclaimerQueue::kInvalidIndex) return;
  // Grab references to the things we'll need
  auto self = Ref(DEBUG_LOCATION, "reclaimer");
  gpr_log(GPR_DEBUG, "%p: register reclaimer; idx=%" PRIdMAX, this,
          reclamation_indices_[0]);
  memory_quota_->reclaimers_[0].Insert(
      self,
      [self](ReclamationSweep) {
        MutexLock lock(&self->memory_quota_mu_);
        // Figure out how many bytes we can return to the quota.
        size_t return_bytes =
            self->free_bytes_.exchange(0, std::memory_order_acq_rel);
        gpr_log(GPR_DEBUG, "%p: sweep reclaimer - return %" PRIdMAX, self.get(),
                return_bytes);
        if (return_bytes == 0) return;
        // Subtract that from our outstanding balance.
        self->taken_bytes_ -= return_bytes;
        // And return them to the quota.
        self->memory_quota_->Return(return_bytes);
      },
      &reclamation_indices_[0]);
}

void MemoryAllocator::Rebind(RefCountedPtr<MemoryQuota> memory_quota) {
  MutexLock lock(&memory_quota_mu_);
  if (memory_quota_ == memory_quota) return;
  // Return memory to the original memory quota.
  memory_quota_->Return(taken_bytes_);
  // Fetch back any reclaimers that are queued.
  ReclamationFunction reclaimers[kNumReclamationPasses];
  for (size_t i = 0; i < kNumReclamationPasses; i++) {
    reclaimers[i] =
        memory_quota_->reclaimers_[i].Cancel(reclamation_indices_[i], this);
  }
  // Switch to the new memory quota, leaving the old one in memory_quota so that
  // when we unref it, we are outside of lock.
  memory_quota_.swap(memory_quota);
  // Drop our freed memory down to zero, to avoid needing to ask the new
  // quota for memory we're not currently using.
  taken_bytes_ -= free_bytes_.exchange(0, std::memory_order_acq_rel);
  // And let the new quota know how much we're already using.
  memory_quota_->Take(taken_bytes_);
  // Reinsert active reclaimers.
  for (size_t i = 0; i < kNumReclamationPasses; i++) {
    if (reclaimers[i] == nullptr) continue;
    memory_quota_->reclaimers_[i].Insert(Ref(DEBUG_LOCATION, "rebind"),
                                         std::move(reclaimers[i]),
                                         &reclamation_indices_[i]);
  }
}

void MemoryAllocator::PostReclaimer(ReclamationPass pass,
                                    ReclamationFunction fn) {
  MutexLock lock(&memory_quota_mu_);
  auto pass_num = static_cast<int>(pass);
  memory_quota_->reclaimers_[pass_num].Insert(
      Ref(DEBUG_LOCATION, "post_reclaimer"), std::move(fn),
      &reclamation_indices_[pass_num]);
}

namespace {

// Reference count for a slice allocated by MemoryAllocator::MakeSlice.
// Takes care of releasing memory back when the slice is destroyed.
class SliceRefCount {
 public:
  static void Destroy(void* p) {
    auto* rc = static_cast<SliceRefCount*>(p);
    rc->~SliceRefCount();
    gpr_free(rc);
  }
  SliceRefCount(RefCountedPtr<MemoryAllocator> allocator, size_t size)
      : base_(grpc_slice_refcount::Type::REGULAR, &refs_, Destroy, this,
              &base_),
        allocator_(std::move(allocator)),
        size_(size) {
    // Nothing to do here.
  }
  ~SliceRefCount() { allocator_->Release(size_); }

  grpc_slice_refcount* base_refcount() { return &base_; }

 private:
  grpc_slice_refcount base_;
  RefCount refs_;
  RefCountedPtr<MemoryAllocator> allocator_;
  size_t size_;
};

}  // namespace

grpc_slice MemoryAllocator::MakeSlice(MemoryRequest request) {
  auto size = Reserve(request.Increase(sizeof(SliceRefCount)));
  void* p = gpr_malloc(size);
  new (p) SliceRefCount(Ref(DEBUG_LOCATION, "slice"), size);
  grpc_slice slice;
  slice.refcount = static_cast<SliceRefCount*>(p)->base_refcount();
  slice.data.refcounted.bytes =
      static_cast<uint8_t*>(p) + sizeof(SliceRefCount);
  slice.data.refcounted.length = size - sizeof(SliceRefCount);
  return slice;
}

//
// MemoryQuota
//

class MemoryQuota::WaitForSweepPromise {
 public:
  WaitForSweepPromise(WeakRefCountedPtr<MemoryQuota> memory_quota,
                      uint64_t token)
      : memory_quota_(memory_quota), token_(token) {}

  struct Empty {};
  Poll<Empty> operator()() {
    if (memory_quota_->reclamation_counter_.load(std::memory_order_relaxed) !=
        token_) {
      return Empty{};
    } else {
      return Pending{};
    }
  }

 private:
  WeakRefCountedPtr<MemoryQuota> memory_quota_;
  uint64_t token_;
};

MemoryQuota::MemoryQuota() : DualRefCounted("MemoryQuota") {
  auto self = WeakRef();

  // Reclamation loop:
  // basically, wait until we are in overcommit (free_bytes_ < 0), and then:
  // while (free_bytes_ < 0) reclaim_memory()
  // ... and repeat
  auto reclamation_loop = Loop(Seq(
      [self]() -> Poll<int> {
        // If there's free memory we no longer need to reclaim memory!
        if (self->free_bytes_.load(std::memory_order_acquire) > 0) {
          return Pending{};
        }
        return 0;
      },
      [self]() {
        // Race biases to the first thing that completes... so this will
        // choose the highest priority/least destructive thing to do that's
        // available.
        return Race(self->reclaimers_[0].Next(), self->reclaimers_[1].Next(),
                    self->reclaimers_[2].Next(), self->reclaimers_[3].Next());
      },
      [self](ReclamationFunction reclaimer) {
        // One of the reclaimer queues gave us a way to get back memory.
        // Call the reclaimer with a token that contains enough to wake us
        // up again.
        const uint64_t token =
            self->reclamation_counter_.fetch_add(1, std::memory_order_relaxed) +
            1;
        reclaimer(ReclamationSweep(self, token));
        // Return a promise that will wait for our barrier. This will be
        // awoken by the token above being destroyed. So, once that token is
        // destroyed, we'll be able to proceed.
        return WaitForSweepPromise(self, token);
      },
      []() -> LoopCtl<absl::Status> {
        // Continue the loop!
        return Continue{};
      }));

  reclaimer_activity_ =
      MakeActivity(std::move(reclamation_loop), ExecCtxWakeupScheduler(),
                   [](absl::Status status) {
                     GPR_ASSERT(status.code() == absl::StatusCode::kCancelled);
                   });
}

void MemoryQuota::SetSize(size_t new_size) {
  size_t old_size = quota_size_.exchange(new_size, std::memory_order_relaxed);
  if (old_size < new_size) {
    // We're growing the quota.
    Return(new_size - old_size);
  } else {
    // We're shrinking the quota.
    Take(old_size - new_size);
  }
}

void MemoryQuota::Take(size_t amount) {
  // If there's a request for nothing, then do nothing!
  if (amount == 0) return;
  GPR_DEBUG_ASSERT(amount <= std::numeric_limits<intptr_t>::max());
  // Grab memory from the quota.
  auto prior = free_bytes_.fetch_sub(amount, std::memory_order_acq_rel);
  // If we push into overcommit, awake the reclaimer.
  if (prior >= 0 && prior < static_cast<intptr_t>(amount)) {
    reclaimer_activity_->ForceWakeup();
  }
}

void MemoryQuota::Orphan() { reclaimer_activity_.reset(); }

void MemoryQuota::FinishReclamation(uint64_t token) {
  uint64_t current = reclamation_counter_.load(std::memory_order_relaxed);
  if (current != token) return;
  if (reclamation_counter_.compare_exchange_strong(current, current + 1,
                                                   std::memory_order_relaxed,
                                                   std::memory_order_relaxed)) {
    reclaimer_activity_->ForceWakeup();
  }
}

void MemoryQuota::Return(size_t amount) {
  free_bytes_.fetch_add(amount, std::memory_order_relaxed);
}

size_t MemoryQuota::InstantaneousPressure() const {
  double free = free_bytes_.load();
  if (free < 0) free = 0;
  double size = quota_size_.load();
  if (size < 1) return 1.0;
  double pressure = (size - free) / size;
  if (pressure < 0.0) pressure = 0.0;
  if (pressure > 1.0) pressure = 1.0;
  return pressure;
}

}  // namespace grpc_core
