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

#include <inttypes.h>

#include <algorithm>
#include <atomic>
#include <tuple>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/trace.h"

namespace grpc_core {

namespace {
// Maximum number of bytes an allocator will request from a quota in one step.
// Larger allocations than this will require multiple allocation requests.
constexpr size_t kMaxReplenishBytes = 1024 * 1024;

// Minimum number of bytes an allocator will request from a quota in one step.
constexpr size_t kMinReplenishBytes = 4096;

class MemoryQuotaTracker {
 public:
  static MemoryQuotaTracker& Get() {
    static MemoryQuotaTracker* tracker = new MemoryQuotaTracker();
    return *tracker;
  }

  void Add(std::shared_ptr<BasicMemoryQuota> quota) {
    MutexLock lock(&mu_);
    // Common usage is that we only create a few (one or two) quotas.
    // We'd like to ensure that we don't OOM if more are added - and
    // using a weak_ptr here, whilst nicely braindead, does run that
    // risk.
    // If usage patterns change sufficiently we'll likely want to
    // change this class to have a more sophisticated data structure
    // and probably a Remove() method.
    GatherAndGarbageCollect();
    quotas_.push_back(quota);
  }

  std::vector<std::shared_ptr<BasicMemoryQuota>> All() {
    MutexLock lock(&mu_);
    return GatherAndGarbageCollect();
  }

 private:
  MemoryQuotaTracker() {}

  std::vector<std::shared_ptr<BasicMemoryQuota>> GatherAndGarbageCollect()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    std::vector<std::weak_ptr<BasicMemoryQuota>> new_quotas;
    std::vector<std::shared_ptr<BasicMemoryQuota>> all_quotas;
    for (const auto& quota : quotas_) {
      auto p = quota.lock();
      if (p == nullptr) continue;
      new_quotas.push_back(quota);
      all_quotas.push_back(p);
    }
    quotas_.swap(new_quotas);
    return all_quotas;
  }

  Mutex mu_;
  std::vector<std::weak_ptr<BasicMemoryQuota>> quotas_ ABSL_GUARDED_BY(mu_);
};
}  // namespace

//
// Reclaimer
//

ReclamationSweep::~ReclamationSweep() {
  if (memory_quota_ != nullptr) {
    memory_quota_->FinishReclamation(sweep_token_, std::move(waker_));
  }
}

//
// ReclaimerQueue
//

struct ReclaimerQueue::QueuedNode
    : public MultiProducerSingleConsumerQueue::Node {
  explicit QueuedNode(RefCountedPtr<Handle> reclaimer_handle)
      : reclaimer_handle(std::move(reclaimer_handle)) {}
  RefCountedPtr<Handle> reclaimer_handle;
};

struct ReclaimerQueue::State {
  Mutex reader_mu;
  MultiProducerSingleConsumerQueue queue;  // reader_mu must be held to pop
  Waker waker ABSL_GUARDED_BY(reader_mu);

  ~State() {
    bool empty = false;
    do {
      delete static_cast<QueuedNode*>(queue.PopAndCheckEnd(&empty));
    } while (!empty);
  }
};

void ReclaimerQueue::Handle::Orphan() {
  if (auto* sweep = sweep_.exchange(nullptr, std::memory_order_acq_rel)) {
    sweep->RunAndDelete(absl::nullopt);
  }
  Unref();
}

void ReclaimerQueue::Handle::Run(ReclamationSweep reclamation_sweep) {
  if (auto* sweep = sweep_.exchange(nullptr, std::memory_order_acq_rel)) {
    sweep->RunAndDelete(std::move(reclamation_sweep));
  }
}

bool ReclaimerQueue::Handle::Requeue(ReclaimerQueue* new_queue) {
  if (sweep_.load(std::memory_order_relaxed)) {
    new_queue->Enqueue(Ref());
    return true;
  } else {
    return false;
  }
}

void ReclaimerQueue::Handle::Sweep::MarkCancelled() {
  // When we cancel a reclaimer we rotate the elements of the queue once -
  // taking one non-cancelled node from the start, and placing it on the end.
  // This ensures that we don't suffer from head of line blocking whereby a
  // non-cancelled reclaimer at the head of the queue, in the absence of memory
  // pressure, prevents the remainder of the queue from being cleaned up.
  MutexLock lock(&state_->reader_mu);
  while (true) {
    bool empty = false;
    std::unique_ptr<QueuedNode> node(
        static_cast<QueuedNode*>(state_->queue.PopAndCheckEnd(&empty)));
    if (node == nullptr) break;
    if (node->reclaimer_handle->sweep_.load(std::memory_order_relaxed) !=
        nullptr) {
      state_->queue.Push(node.release());
      break;
    }
  }
}

ReclaimerQueue::ReclaimerQueue() : state_(std::make_shared<State>()) {}

ReclaimerQueue::~ReclaimerQueue() = default;

void ReclaimerQueue::Enqueue(RefCountedPtr<Handle> handle) {
  if (state_->queue.Push(new QueuedNode(std::move(handle)))) {
    MutexLock lock(&state_->reader_mu);
    state_->waker.Wakeup();
  }
}

Poll<RefCountedPtr<ReclaimerQueue::Handle>> ReclaimerQueue::PollNext() {
  MutexLock lock(&state_->reader_mu);
  bool empty = false;
  // Try to pull from the queue.
  std::unique_ptr<QueuedNode> node(
      static_cast<QueuedNode*>(state_->queue.PopAndCheckEnd(&empty)));
  // If we get something, great.
  if (node != nullptr) return std::move(node->reclaimer_handle);
  if (!empty) {
    // If we don't, but the queue is probably not empty, schedule an immediate
    // repoll.
    Activity::current()->ForceImmediateRepoll();
  } else {
    // Otherwise, schedule a wakeup for whenever something is pushed.
    state_->waker = Activity::current()->MakeNonOwningWaker();
  }
  return Pending{};
}

//
// GrpcMemoryAllocatorImpl
//

GrpcMemoryAllocatorImpl::GrpcMemoryAllocatorImpl(
    std::shared_ptr<BasicMemoryQuota> memory_quota, std::string name)
    : memory_quota_(memory_quota), name_(std::move(name)) {
  memory_quota_->Take(
      /*allocator=*/this, taken_bytes_);
  memory_quota_->AddNewAllocator(this);
}

GrpcMemoryAllocatorImpl::~GrpcMemoryAllocatorImpl() {
  GPR_ASSERT(free_bytes_.load(std::memory_order_acquire) +
                 sizeof(GrpcMemoryAllocatorImpl) ==
             taken_bytes_.load(std::memory_order_relaxed));
  memory_quota_->Return(taken_bytes_);
}

void GrpcMemoryAllocatorImpl::Shutdown() {
  memory_quota_->RemoveAllocator(this);
  std::shared_ptr<BasicMemoryQuota> memory_quota;
  OrphanablePtr<ReclaimerQueue::Handle>
      reclamation_handles[kNumReclamationPasses];
  {
    MutexLock lock(&reclaimer_mu_);
    GPR_ASSERT(!shutdown_);
    shutdown_ = true;
    memory_quota = memory_quota_;
    for (size_t i = 0; i < kNumReclamationPasses; i++) {
      reclamation_handles[i] = std::exchange(reclamation_handles_[i], nullptr);
    }
  }
}

size_t GrpcMemoryAllocatorImpl::Reserve(MemoryRequest request) {
  // Validate request - performed here so we don't bloat the generated code with
  // inlined asserts.
  GPR_ASSERT(request.min() <= request.max());
  GPR_ASSERT(request.max() <= MemoryRequest::max_allowed_size());
  size_t old_free = free_bytes_.load(std::memory_order_relaxed);

  while (true) {
    // Attempt to reserve memory from our pool.
    auto reservation = TryReserve(request);
    if (reservation.has_value()) {
      size_t new_free = free_bytes_.load(std::memory_order_relaxed);
      memory_quota_->MaybeMoveAllocator(this, old_free, new_free);
      return *reservation;
    }

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
    const auto pressure_info = memory_quota_->GetPressureInfo();
    double pressure = pressure_info.pressure_control_value;
    size_t max_recommended_allocation_size =
        pressure_info.max_recommended_allocation_size;
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

void GrpcMemoryAllocatorImpl::MaybeDonateBack() {
  size_t free = free_bytes_.load(std::memory_order_relaxed);
  while (free > 0) {
    size_t ret = 0;
    if (!IsUnconstrainedMaxQuotaBufferSizeEnabled() &&
        free > kMaxQuotaBufferSize / 2) {
      ret = std::max(ret, free - kMaxQuotaBufferSize / 2);
    }
    ret = std::max(ret, free > 8192 ? free / 2 : free);
    const size_t new_free = free - ret;
    if (free_bytes_.compare_exchange_weak(free, new_free,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
        gpr_log(GPR_INFO, "[%p|%s] Early return %" PRIdPTR " bytes", this,
                name_.c_str(), ret);
      }
      GPR_ASSERT(taken_bytes_.fetch_sub(ret, std::memory_order_relaxed) >= ret);
      memory_quota_->Return(ret);
      return;
    }
  }
}

void GrpcMemoryAllocatorImpl::Replenish() {
  // Attempt a fairly low rate exponential growth request size, bounded between
  // some reasonable limits declared at top of file.
  auto amount = Clamp(taken_bytes_.load(std::memory_order_relaxed) / 3,
                      kMinReplenishBytes, kMaxReplenishBytes);
  // Take the requested amount from the quota.
  memory_quota_->Take(
      /*allocator=*/this, amount);
  // Record that we've taken it.
  taken_bytes_.fetch_add(amount, std::memory_order_relaxed);
  // Add the taken amount to the free pool.
  free_bytes_.fetch_add(amount, std::memory_order_acq_rel);
}

//
// BasicMemoryQuota
//

class BasicMemoryQuota::WaitForSweepPromise {
 public:
  WaitForSweepPromise(std::shared_ptr<BasicMemoryQuota> memory_quota,
                      uint64_t token)
      : memory_quota_(std::move(memory_quota)), token_(token) {}

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

BasicMemoryQuota::BasicMemoryQuota(std::string name) : name_(std::move(name)) {}

void BasicMemoryQuota::Start() {
  auto self = shared_from_this();

  MemoryQuotaTracker::Get().Add(self);

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
          return [name](RefCountedPtr<ReclaimerQueue::Handle> f) {
            return std::make_tuple(name, std::move(f));
          };
        };
        return Race(Map(self->reclaimers_[0].Next(), annotate("benign")),
                    Map(self->reclaimers_[1].Next(), annotate("idle")),
                    Map(self->reclaimers_[2].Next(), annotate("destructive")));
      },
      [self](
          std::tuple<const char*, RefCountedPtr<ReclaimerQueue::Handle>> arg) {
        auto reclaimer = std::move(std::get<1>(arg));
        if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
          double free = std::max(intptr_t{0}, self->free_bytes_.load());
          size_t quota_size = self->quota_size_.load();
          gpr_log(GPR_INFO,
                  "RQ: %s perform %s reclamation. Available free bytes: %f, "
                  "total quota_size: %zu",
                  self->name_.c_str(), std::get<0>(arg), free, quota_size);
        }
        // One of the reclaimer queues gave us a way to get back memory.
        // Call the reclaimer with a token that contains enough to wake us
        // up again.
        const uint64_t token =
            self->reclamation_counter_.fetch_add(1, std::memory_order_relaxed) +
            1;
        reclaimer->Run(ReclamationSweep(
            self, token, Activity::current()->MakeNonOwningWaker()));
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
    Take(/*allocator=*/nullptr, old_size - new_size);
  }
}

void BasicMemoryQuota::Take(GrpcMemoryAllocatorImpl* allocator, size_t amount) {
  // If there's a request for nothing, then do nothing!
  if (amount == 0) return;
  GPR_DEBUG_ASSERT(amount <= std::numeric_limits<intptr_t>::max());
  // Grab memory from the quota.
  auto prior = free_bytes_.fetch_sub(amount, std::memory_order_acq_rel);
  // If we push into overcommit, awake the reclaimer.
  if (prior >= 0 && prior < static_cast<intptr_t>(amount)) {
    if (reclaimer_activity_ != nullptr) reclaimer_activity_->ForceWakeup();
  }

  if (IsFreeLargeAllocatorEnabled()) {
    if (allocator == nullptr) return;
    GrpcMemoryAllocatorImpl* chosen_allocator = nullptr;
    // Use calling allocator's shard index to choose shard.
    auto& shard = big_allocators_.shards[allocator->IncrementShardIndex() %
                                         big_allocators_.shards.size()];

    if (shard.shard_mu.TryLock()) {
      if (!shard.allocators.empty()) {
        chosen_allocator = *shard.allocators.begin();
      }
      shard.shard_mu.Unlock();
    }

    if (chosen_allocator != nullptr) {
      chosen_allocator->ReturnFree();
    }
  }
}

void BasicMemoryQuota::FinishReclamation(uint64_t token, Waker waker) {
  uint64_t current = reclamation_counter_.load(std::memory_order_relaxed);
  if (current != token) return;
  if (reclamation_counter_.compare_exchange_strong(current, current + 1,
                                                   std::memory_order_relaxed,
                                                   std::memory_order_relaxed)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
      double free = std::max(intptr_t{0}, free_bytes_.load());
      size_t quota_size = quota_size_.load();
      gpr_log(GPR_INFO,
              "RQ: %s reclamation complete. Available free bytes: %f, "
              "total quota_size: %zu",
              name_.c_str(), free, quota_size);
    }
    waker.Wakeup();
  }
}

void BasicMemoryQuota::Return(size_t amount) {
  free_bytes_.fetch_add(amount, std::memory_order_relaxed);
}

void BasicMemoryQuota::AddNewAllocator(GrpcMemoryAllocatorImpl* allocator) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
    gpr_log(GPR_INFO, "Adding allocator %p", allocator);
  }

  AllocatorBucket::Shard& shard = small_allocators_.SelectShard(allocator);

  {
    MutexLock l(&shard.shard_mu);
    shard.allocators.emplace(allocator);
  }
}

void BasicMemoryQuota::RemoveAllocator(GrpcMemoryAllocatorImpl* allocator) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
    gpr_log(GPR_INFO, "Removing allocator %p", allocator);
  }

  AllocatorBucket::Shard& small_shard =
      small_allocators_.SelectShard(allocator);

  {
    MutexLock l(&small_shard.shard_mu);
    if (small_shard.allocators.erase(allocator) == 1) {
      return;
    }
  }

  AllocatorBucket::Shard& big_shard = big_allocators_.SelectShard(allocator);

  {
    MutexLock l(&big_shard.shard_mu);
    big_shard.allocators.erase(allocator);
  }
}

void BasicMemoryQuota::MaybeMoveAllocator(GrpcMemoryAllocatorImpl* allocator,
                                          size_t old_free_bytes,
                                          size_t new_free_bytes) {
  while (true) {
    if (new_free_bytes < kSmallAllocatorThreshold) {
      // Still in small bucket. No move.
      if (old_free_bytes < kSmallAllocatorThreshold) return;
      MaybeMoveAllocatorBigToSmall(allocator);
    } else if (new_free_bytes > kBigAllocatorThreshold) {
      // Still in big bucket. No move.
      if (old_free_bytes > kBigAllocatorThreshold) return;
      MaybeMoveAllocatorSmallToBig(allocator);
    } else {
      // Somewhere between thresholds. No move.
      return;
    }

    // Loop to make sure move is eventually stable.
    old_free_bytes = new_free_bytes;
    new_free_bytes = allocator->GetFreeBytes();
  }
}

void BasicMemoryQuota::MaybeMoveAllocatorBigToSmall(
    GrpcMemoryAllocatorImpl* allocator) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
    gpr_log(GPR_INFO, "Moving allocator %p to small", allocator);
  }

  AllocatorBucket::Shard& old_shard = big_allocators_.SelectShard(allocator);

  {
    MutexLock l(&old_shard.shard_mu);
    if (old_shard.allocators.erase(allocator) == 0) return;
  }

  AllocatorBucket::Shard& new_shard = small_allocators_.SelectShard(allocator);

  {
    MutexLock l(&new_shard.shard_mu);
    new_shard.allocators.emplace(allocator);
  }
}

void BasicMemoryQuota::MaybeMoveAllocatorSmallToBig(
    GrpcMemoryAllocatorImpl* allocator) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
    gpr_log(GPR_INFO, "Moving allocator %p to big", allocator);
  }

  AllocatorBucket::Shard& old_shard = small_allocators_.SelectShard(allocator);

  {
    MutexLock l(&old_shard.shard_mu);
    if (old_shard.allocators.erase(allocator) == 0) return;
  }

  AllocatorBucket::Shard& new_shard = big_allocators_.SelectShard(allocator);

  {
    MutexLock l(&new_shard.shard_mu);
    new_shard.allocators.emplace(allocator);
  }
}

BasicMemoryQuota::PressureInfo BasicMemoryQuota::GetPressureInfo() {
  double free = free_bytes_.load();
  if (free < 0) free = 0;
  size_t quota_size = quota_size_.load();
  double size = quota_size;
  if (size < 1) return PressureInfo{1, 1, 1};
  PressureInfo pressure_info;
  pressure_info.instantaneous_pressure = std::max(0.0, (size - free) / size);
  if (IsMemoryPressureControllerEnabled()) {
    pressure_info.pressure_control_value =
        pressure_tracker_.AddSampleAndGetControlValue(
            pressure_info.instantaneous_pressure);
  } else {
    pressure_info.pressure_control_value =
        std::min(pressure_info.instantaneous_pressure, 1.0);
  }
  pressure_info.max_recommended_allocation_size = quota_size / 16;
  return pressure_info;
}

//
// PressureTracker
//

namespace memory_quota_detail {

double PressureController::Update(double error) {
  bool is_low = error < 0;
  bool was_low = std::exchange(last_was_low_, is_low);
  double new_control;  // leave unset to compiler can note bad branches
  if (is_low && was_low) {
    // Memory pressure is too low this round, and was last round too.
    // If we have reached the min reporting value last time, then we will report
    // the same value again this time and can start to increase the ticks_same_
    // counter.
    if (last_control_ == min_) {
      ticks_same_++;
      if (ticks_same_ >= max_ticks_same_) {
        // If it's been the same for too long, reduce the min reported value
        // down towards zero.
        min_ /= 2.0;
        ticks_same_ = 0;
      }
    }
    // Target the min reporting value.
    new_control = min_;
  } else if (!is_low && !was_low) {
    // Memory pressure is high, and was high previously.
    ticks_same_++;
    if (ticks_same_ >= max_ticks_same_) {
      // It's been high for too long, increase the max reporting value up
      // towards 1.0.
      max_ = (1.0 + max_) / 2.0;
      ticks_same_ = 0;
    }
    // Target the max reporting value.
    new_control = max_;
  } else if (is_low) {
    // Memory pressure is low, but was high last round.
    // Target the min reporting value, but first update it to be closer to the
    // current max (that we've been reporting lately).
    // In this way the min will gradually climb towards the max as we find a
    // stable point.
    // If this is too high, then we'll eventually move it back towards zero.
    ticks_same_ = 0;
    min_ = (min_ + max_) / 2.0;
    new_control = min_;
  } else {
    // Memory pressure is high, but was low last round.
    // Target the max reporting value, but first update it to be closer to the
    // last reported value.
    // The first switchover will have last_control_ being 0, and max_ being 2,
    // so we'll immediately choose 1.0 which will tend to really slow down
    // progress.
    // If we end up targetting too low, we'll eventually move it back towards
    // 1.0 after max_ticks_same_ ticks.
    ticks_same_ = 0;
    max_ = (last_control_ + max_) / 2.0;
    new_control = max_;
  }
  // If the control value is decreasing we do it slowly. This avoids rapid
  // oscillations.
  // (If we want a control value that's higher than the last one we snap
  // immediately because it's likely that memory pressure is growing unchecked).
  if (new_control < last_control_) {
    new_control =
        std::max(new_control, last_control_ - max_reduction_per_tick_ / 1000.0);
  }
  last_control_ = new_control;
  return new_control;
}

std::string PressureController::DebugString() const {
  return absl::StrCat(last_was_low_ ? "low" : "high", " min=", min_,
                      " max=", max_, " ticks=", ticks_same_,
                      " last_control=", last_control_);
}

double PressureTracker::AddSampleAndGetControlValue(double sample) {
  static const double kSetPoint = 0.95;

  double max_so_far = max_this_round_.load(std::memory_order_relaxed);
  if (sample > max_so_far) {
    max_this_round_.compare_exchange_weak(max_so_far, sample,
                                          std::memory_order_relaxed,
                                          std::memory_order_relaxed);
  }
  // If memory pressure is almost done, immediately hit the brakes and report
  // full memory usage.
  if (sample >= 0.99) {
    report_.store(1.0, std::memory_order_relaxed);
  }
  update_.Tick([&](Duration) {
    // Reset the round tracker with the new sample.
    const double current_estimate =
        max_this_round_.exchange(sample, std::memory_order_relaxed);
    double report;
    if (current_estimate > 0.99) {
      // Under very high memory pressure we... just max things out.
      report = controller_.Update(1e99);
    } else {
      report = controller_.Update(current_estimate - kSetPoint);
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_resource_quota_trace)) {
      gpr_log(GPR_INFO, "RQ: pressure:%lf report:%lf controller:%s",
              current_estimate, report, controller_.DebugString().c_str());
    }
    report_.store(report, std::memory_order_relaxed);
  });
  return report_.load(std::memory_order_relaxed);
}

}  // namespace memory_quota_detail

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

std::vector<std::shared_ptr<BasicMemoryQuota>> AllMemoryQuotas() {
  return MemoryQuotaTracker::Get().All();
}

}  // namespace grpc_core
