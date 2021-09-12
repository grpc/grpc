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

#include "src/core/lib/memory_quota/memory_quota.h"

namespace grpc_core {

//
// Reclaimer
//

Reclaimer::Token::~Token() {
  if (memory_quota_ != nullptr) {
    memory_quota_->EndReclamation(id_);
  }
}

void Reclaimer::Arm(ReclaimFunction fn) {
#ifndef NDEBUG
  GPR_ASSERT(!armed_.exchange(true, std::memory_order_acq_rel));
  auto ref = Ref();
  fn = Capture([ref](ReclaimFunction* f, Token token) {
    GPR_ASSERT(armed_.exchange(false, std::memory_order_acq_rel));
    (*f)(std::move(token));
  });
#endif
  memory_quota_->GetPipe(pass_)->Emplace(std::move(fn));
}

//
// MemoryRequest
//

namespace {
size_t RoundUp(size_t size, size_t block_size) {
  return (size + block_size - 1) / block_size * block_size;
}
size_t RoundDown(size_t size, size_t block_size) {
  return size / block_size * block_size;
}
}  // namespace

MemoryRequest::WithBlockSize(size_t block_size) const {
  Request r(RoundUp(min_, block_size), RoundDown(max_, block_size));
  r.block_size = block_size;
  return r;
}

//
// MemoryAllocator
//

MemoryAllocator::MemoryAllocator(MemoryQuotaPtr memory_quota)
    : memory_quota_(memory_quota) {}

MemoryAllocator::~MemoryAllocator() {
  GPR_ASSERT(free_bytes_.load(std::memory_order_acquire) == 0);
  memory_quota_->Return(taken_bytes_);
}

size_t MemoryAllocator::Reserve(Request request) {
  while (true) {
    // Attempt to reserve memory from our pool.
    auto reservation = TryReserve(request);
    if (reservation.success) return reservation.size;
    // If that failed, grab more from the quota and retry.
    Replenish(reservation.size);
  }
}

MemoryAllocator::ReserveResult MemoryAllocator::TryReserve(Request request) {
  // How much memory should we request? (see the scaling below)
  size_t scaled_request = request.max();
  // If we don't get what we want, how much should we request from the quota?
  // We actually ask for more than what we need to avoid needing to ask again.
  size_t take_request = 2 * request.max();
  // Scale the request down according to memory pressure if we have that
  // flexibility.
  if (request.min() != request.max()) {
    double pressure =
        (absl::MutexLock(&memory_quota_mu_), memory_quota_->GetPressure());
    // Reduce allocation size proportional to the pressure > 80% usage.
    if (pressure > 0.8) {
      scaled_request =
          std::min(request.max(),
                   RoundUp(request.min() + (request.max() - request.min()) *
                                               (1.0 - pressure) / 0.2,
                           request.block_size));
      take_request = request.min();
    }
  }

  // See how many bytes are available.
  size_t available = free_bytes_.load(std::memory_order_acquire);
  while (true) {
    // Does the current free pool satisfy the request?
    const size_t rounded_available = RoundDown(available, request.block_size());
    if (rounded_available < request.min()) {
      return {false, take_request - available};
    }
    // If so, grab as much as we need, up to what's available.
    const size_t reserve = std::min(rounded_available, scaled_request);
    // Try to reserve the requested amount.
    // If the amount of free memory changed through this loop, then available
    // will be set to the new value and we'll repeat.
    if (free_bytes_.compare_exchange_weak(available, available - reserve,
                                          std::memory_order_acq_rel,
                                          std::memory_order_release)) {
      return {true, reserve};
    }
  }
}

void MemoryAllocator::Replenish(size_t amount) {
  absl::MutexLock lock(&memory_quota_mu_);
  // Take the requested amount from the quota.
  memory_quota_->Take(amount);
  // Record that we've taken it.
  taken_bytes_ += amount;
  // Add the taken amount to the free pool.
  free_bytes_.fetch_add(amount, std::memory_order_acq_rel);
  // See if we can add ourselves as a reclaimer.
  MaybeRegisterReclaimerLocked();
}

void MemoryAllocator::MaybeRegisterReclaimer() {
  absl::MutexLock lock(&memory_quota_mu_);
  MaybeRegisterReclaimerLocked();
}

void MemoryAllocator::MaybeRegisterReclaimerLocked() {
  // If the reclaimer is already registered, then there's nothing to do.
  if (absl::exchange(reclaimer_armed_, true)) return;
  // Grab references to the things we'll need
  auto self = Ref();
  auto memory_quota = memory_quota_;
  memory_quota_->non_empty_reclaimers_.Push(
      [self, memory_quota](Reclaimer::Token) {
        absl::MutexLock lock(&self->memory_quota_mu_);
        // If the allocator's quota changed since this function was scheduled,
        // there's nothing more to do here.
        if (self->memory_quota_ != memory_quota) return;
        // Signal that we're no longer armed.
        self->reclaimer_armed_ = false;
        // Figure out how many bytes we can return to the quota.
        size_t return_bytes =
            self->free_bytes_.exchange(0, std::memory_order_acq_rel);
        // Subtract that from our outstanding balance.
        taken_bytes_ -= return_bytes;
        // And return them to the quota.
        self->memory_quota_->Return(return_bytes);
      });
}

void MemoryAllocator::Rebind(MemoryQuotaPtr memory_quota) {
  absl::MutexLock lock(&memory_quota_mu_);
  if (memory_quota_ == memory_quota) return;
  // Return memory to the original memory quota.
  memory_quota_->Return(taken_bytes_);
  // Switch to the new memory quota.
  memory_quota_ = std::move(memory_quota);
  reclaimer_armed_ = false;
  // Drop our freed memory down to zero, to avoid needing to ask the new
  // quota for memory we're not currently using.
  taken_bytes_ -= free_bytes_.exchange(0, std::memory_order_acq_rel);
  // And let the new quota know how much we're already using.
  memory_quota_->TakeImmediately(taken_bytes_);
}

//
// MemoryQuota
//

MemoryQuota::MemoryQuota() {
  auto self = Ref();

  struct Empty {};
  auto reclaim_loop = Loop(Seq(
      [self]() -> Poll<Empty> {
        // If there's free memory we no longer need to reclaim memory!
        if (self->free_bytes_.load(std::memory_order_acquire) > 0) {
          return Pending{};
        }
        return Empty{};
      },
      [self]() {
        // Race biases to the first thing that completes... so this will choose
        // the highest priority/least destructive thing to do that's available.
        return Race(non_empty_reclaimers_.Next(), benign_reclaimers_.Next(),
                    idle_reclaimers_.Next(), destructive_reclaimers_.Next());
      },
      [self](Reclaimer::ReclaimFunction reclaimer) {
        // One of the reclaimer queues gave us a way to get back memory.
        // Call the reclaimer with a token that contains enough to wake us up
        // again.
        reclaimer(Reclaimer::Token(self->Ref(), self->barrier_.NewToken()));
        // Return a promise that will wait for our barrier. This will be awoken
        // by the token above being destroyed.
        // So, once that token is destroyed, we'll be able to proceed.
        return self->barrier_.Wait();
      },
      [] {
        // Continue the loop!
        return Continue{};
      }));

  activity_ = MakeActivity(
      reclaim_loop, [](std::function<void> f) { std::thread(f).detach(); },
      [] { abort(); });
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
  // Grab memory from the quota.
  auto prior = free_bytes_.fetch_sub(amount, std::memory_order_acq_rel);
  // If we push into overcommit, awake the reclaimer.
  if (prior >= 0 && prior < amount) {
    reclaimer_activity_->Wakeup();
  }
}

void MemoryQuota::Orphan() { activity_.reset(); }

void MemoryQuota::FinishReclamation(uint64_t token) { barrier_.Notify(token); }

void MemoryQuota::Return(size_t amount) {
  free_bytes_.fetch_add(amount, std::memory_order_relaxed);
}

}  // namespace grpc_core
