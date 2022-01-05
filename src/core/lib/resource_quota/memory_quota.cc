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
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/trace.h"

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

void ReclaimerQueue::Insert(
    std::shared_ptr<EventEngineMemoryAllocatorImpl> allocator,
    ReclamationFunction reclaimer, Index* index) {
  ReleasableMutexLock lock(&mu_);
  if (*index < entries_.size() && entries_[*index].allocator == allocator) {
    entries_[*index].reclaimer.swap(reclaimer);
    lock.Release();
    reclaimer({});
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

ReclamationFunction ReclaimerQueue::Cancel(
    Index index, EventEngineMemoryAllocatorImpl* allocator) {
  MutexLock lock(&mu_);
  if (index >= entries_.size()) return nullptr;
  Entry& entry = entries_[index];
  if (entry.allocator.get() != allocator) return {};
  entry.allocator.reset();
  free_entries_.push_back(index);
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
// GrpcMemoryAllocatorImpl
//

GrpcMemoryAllocatorImpl::GrpcMemoryAllocatorImpl(
    std::shared_ptr<BasicMemoryQuota> memory_quota, std::string name)
    : memory_quota_(memory_quota), name_(std::move(name)) {
  memory_quota_->Take(taken_bytes_);
}

GrpcMemoryAllocatorImpl::~GrpcMemoryAllocatorImpl() {
  GPR_ASSERT(free_bytes_.load(std::memory_order_acquire) +
                 sizeof(GrpcMemoryAllocatorImpl) ==
             taken_bytes_);
  memory_quota_->Return(taken_bytes_);
}

void GrpcMemoryAllocatorImpl::Shutdown() {
  std::shared_ptr<BasicMemoryQuota> memory_quota;
  ReclaimerQueue::Index reclamation_indices[kNumReclamationPasses];
  {
    MutexLock lock(&memory_quota_mu_);
    GPR_ASSERT(!shutdown_);
    shutdown_ = true;
    memory_quota = memory_quota_;
    for (size_t i = 0; i < kNumReclamationPasses; i++) {
      reclamation_indices[i] = absl::exchange(reclamation_indices_[i],
                                              ReclaimerQueue::kInvalidIndex);
    }
  }
  for (size_t i = 0; i < kNumReclamationPasses; i++) {
    auto fn = memory_quota->CancelReclaimer(i, reclamation_indices[i], this);
    if (fn != nullptr) fn({});
  }
}

size_t GrpcMemoryAllocatorImpl::Reserve(MemoryRequest request) {
  // Validate request - performed here so we don't bloat the generated code with
  // inlined asserts.
  GPR_ASSERT(request.min() <= request.max());
  GPR_ASSERT(request.max() <= MemoryRequest::max_allowed_size());
  while (true) {
    // Attempt to reserve memory from our pool.
    auto reservation = TryReserve(request);
    if (reservation.has_value()) return *reservation;
    // If that failed, grab more from the quota and retry.
    Replenish();
  }
}

absl::optional<size_t> GrpcMemoryAllocatorImpl::TryReserve(
    MemoryRequest request) {
  // How much memory should we request? (see the scaling below)
  size_t scaled_size_over_min = request.max() - request.min();
  // Scale the request down according to memory pressure if we have that
  // flexibility.
  if (scaled_size_over_min != 0) {
    double pressure;
    size_t max_recommended_allocation_size;
    {
      MutexLock lock(&memory_quota_mu_);
      const auto pressure_and_max_recommended_allocation_size =
          memory_quota_->InstantaneousPressureAndMaxRecommendedAllocationSize();
      pressure = pressure_and_max_recommended_allocation_size.first;
      max_recommended_allocation_size =
          pressure_and_max_recommended_allocation_size.second;
    }
    // Reduce allocation size proportional to the pressure > 80% usage.
    if (pressure > 0.8) {
      scaled_size_over_min =
          std::min(scaled_size_over_min,
                   static_cast<size_t>((request.max() - request.min()) *
                                       (1.0 - pressure) / 0.2));
    }
    if (max_recommended_allocation_size < request.min()) {
      scaled_size_over_min = 0;
    } else if (request.min() + scaled_size_over_min >
               max_recommended_allocation_size) {
      scaled_size_over_min = max_recommended_allocation_size - request.min();
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

void GrpcMemoryAllocatorImpl::Replenish() {
  MutexLock lock(&memory_quota_mu_);
  GPR_ASSERT(!shutdown_);
  // Attempt a fairly low rate exponential growth request size, bounded between
  // some reasonable limits declared at top of file.
  auto amount = Clamp(taken_bytes_ / 3, kMinReplenishBytes, kMaxReplenishBytes);
  // Take the requested amount from the quota.
  memory_quota_->Take(amount);
  // Record that we've taken it.
  taken_bytes_ += amount;
  // Add the taken amount to the free pool.
  free_bytes_.fetch_add(amount, std::memory_order_acq_rel);
  // See if we can add ourselves as a reclaimer.
  MaybeRegisterReclaimerLocked();
}

void GrpcMemoryAllocatorImpl::MaybeRegisterReclaimer() {
  MutexLock lock(&memory_quota_mu_);
  MaybeRegisterReclaimerLocked();
}

void GrpcMemoryAllocatorImpl::MaybeRegisterReclaimerLocked() {
  // If the reclaimer is already registered, then there's nothing to do.
  if (reclamation_indices_[0] != ReclaimerQueue::kInvalidIndex) return;
  if (shutdown_) return;
  // Grab references to the things we'll need
  auto self = shared_from_this();
  memory_quota_->InsertReclaimer(
      0, self,
      [self](absl::optional<ReclamationSweep> sweep) {
        if (!sweep.has_value()) return;
        auto* p = static_cast<GrpcMemoryAllocatorImpl*>(self.get());
        MutexLock lock(&p->memory_quota_mu_);
        // Figure out how many bytes we can return to the quota.
        size_t return_bytes =
            p->free_bytes_.exchange(0, std::memory_order_acq_rel);
        if (return_bytes == 0) return;
        // Subtract that from our outstanding balance.
        p->taken_bytes_ -= return_bytes;
        // And return them to the quota.
        p->memory_quota_->Return(return_bytes);
      },
      &reclamation_indices_[0]);
}

void GrpcMemoryAllocatorImpl::Rebind(
    std::shared_ptr<BasicMemoryQuota> memory_quota) {
  MutexLock lock(&memory_quota_mu_);
  GPR_ASSERT(!shutdown_);
  if (memory_quota_ == memory_quota) return;
  // Return memory to the original memory quota.
  memory_quota_->Return(taken_bytes_);
  // Fetch back any reclaimers that are queued.
  ReclamationFunction reclaimers[kNumReclamationPasses];
  for (size_t i = 0; i < kNumReclamationPasses; i++) {
    reclaimers[i] =
        memory_quota_->CancelReclaimer(i, reclamation_indices_[i], this);
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
    memory_quota_->InsertReclaimer(i, shared_from_this(),
                                   std::move(reclaimers[i]),
                                   &reclamation_indices_[i]);
  }
}

void GrpcMemoryAllocatorImpl::PostReclaimer(ReclamationPass pass,
                                            ReclamationFunction fn) {
  MutexLock lock(&memory_quota_mu_);
  GPR_ASSERT(!shutdown_);
  auto pass_num = static_cast<size_t>(pass);
  memory_quota_->InsertReclaimer(pass_num, shared_from_this(), std::move(fn),
                                 &reclamation_indices_[pass_num]);
}

//
// MemoryOwner
//

void MemoryOwner::Rebind(MemoryQuota* quota) {
  impl()->Rebind(quota->memory_quota_);
}

//
// BasicMemoryQuota
//

class BasicMemoryQuota::WaitForSweepPromise {
 public:
  WaitForSweepPromise(std::shared_ptr<BasicMemoryQuota> memory_quota,
                      uint64_t token)
      : memory_quota_(std::move(memory_quota)), token_(token) {}

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
  std::shared_ptr<BasicMemoryQuota> memory_quota_;
  uint64_t token_;
};

void BasicMemoryQuota::Start() {
  auto self = shared_from_this();

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
        auto annotate = [](const char* name) {
          return [name](ReclamationFunction f) {
            return std::make_tuple(name, std::move(f));
          };
        };
        return Race(Map(self->reclaimers_[0].Next(), annotate("compact")),
                    Map(self->reclaimers_[1].Next(), annotate("benign")),
                    Map(self->reclaimers_[2].Next(), annotate("idle")),
                    Map(self->reclaimers_[3].Next(), annotate("destructive")));
      },
      [self](std::tuple<const char*, ReclamationFunction> arg) {
        auto reclaimer = std::move(std::get<1>(arg));
        if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
          gpr_log(GPR_INFO, "RQ: %s perform %s reclamation",
                  self->name_.c_str(), std::get<0>(arg));
        }
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

void BasicMemoryQuota::Stop() { reclaimer_activity_.reset(); }

void BasicMemoryQuota::SetSize(size_t new_size) {
  size_t old_size = quota_size_.exchange(new_size, std::memory_order_relaxed);
  if (old_size < new_size) {
    // We're growing the quota.
    Return(new_size - old_size);
  } else {
    // We're shrinking the quota.
    Take(old_size - new_size);
  }
}

void BasicMemoryQuota::Take(size_t amount) {
  // If there's a request for nothing, then do nothing!
  if (amount == 0) return;
  GPR_DEBUG_ASSERT(amount <= std::numeric_limits<intptr_t>::max());
  // Grab memory from the quota.
  auto prior = free_bytes_.fetch_sub(amount, std::memory_order_acq_rel);
  // If we push into overcommit, awake the reclaimer.
  if (prior >= 0 && prior < static_cast<intptr_t>(amount)) {
    if (reclaimer_activity_ != nullptr) reclaimer_activity_->ForceWakeup();
  }
}

void BasicMemoryQuota::FinishReclamation(uint64_t token) {
  uint64_t current = reclamation_counter_.load(std::memory_order_relaxed);
  if (current != token) return;
  if (reclamation_counter_.compare_exchange_strong(current, current + 1,
                                                   std::memory_order_relaxed,
                                                   std::memory_order_relaxed)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
      gpr_log(GPR_INFO, "RQ: %s reclamation complete", name_.c_str());
    }
    if (reclaimer_activity_ != nullptr) reclaimer_activity_->ForceWakeup();
  }
}

void BasicMemoryQuota::Return(size_t amount) {
  free_bytes_.fetch_add(amount, std::memory_order_relaxed);
}

std::pair<double, size_t>
BasicMemoryQuota::InstantaneousPressureAndMaxRecommendedAllocationSize() const {
  double free = free_bytes_.load();
  if (free < 0) free = 0;
  size_t quota_size = quota_size_.load();
  double size = quota_size;
  if (size < 1) return std::make_pair(1.0, 1);
  double pressure = (size - free) / size;
  if (pressure < 0.0) pressure = 0.0;
  if (pressure > 1.0) pressure = 1.0;
  return std::make_pair(pressure, quota_size / 16);
}

//
// MemoryQuota
//

MemoryAllocator MemoryQuota::CreateMemoryAllocator(absl::string_view name) {
  auto impl = std::make_shared<GrpcMemoryAllocatorImpl>(
      memory_quota_, absl::StrCat(memory_quota_->name(), "/allocator/", name));
  return MemoryAllocator(std::move(impl));
}

MemoryOwner MemoryQuota::CreateMemoryOwner(absl::string_view name) {
  auto impl = std::make_shared<GrpcMemoryAllocatorImpl>(
      memory_quota_, absl::StrCat(memory_quota_->name(), "/owner/", name));
  return MemoryOwner(std::move(impl));
}

}  // namespace grpc_core
