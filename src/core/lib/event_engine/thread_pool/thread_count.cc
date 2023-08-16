// Copyright 2023 The gRPC Authors
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

#include "src/core/lib/event_engine/thread_pool/thread_count.h"

#include <inttypes.h>

#include <atomic>
#include <numeric>
#include <utility>

#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/support/cpu.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"

namespace grpc_event_engine {
namespace experimental {

// -------- BusyThreadCount --------

BusyThreadCount::AutoThreadCounter::AutoThreadCounter(BusyThreadCount* counter,
                                                      size_t idx)
    : counter_(counter), idx_(idx) {
  counter_->Increment(idx_);
}

BusyThreadCount::AutoThreadCounter::~AutoThreadCounter() {
  if (counter_ != nullptr) counter_->Decrement(idx_);
}

BusyThreadCount::AutoThreadCounter::AutoThreadCounter(
    BusyThreadCount::AutoThreadCounter&& other) noexcept {
  counter_ = std::exchange(other.counter_, nullptr);
  idx_ = other.idx_;
}

BusyThreadCount::AutoThreadCounter&
BusyThreadCount::AutoThreadCounter::operator=(
    BusyThreadCount::AutoThreadCounter&& other) noexcept {
  counter_ = std::exchange(other.counter_, nullptr);
  idx_ = other.idx_;
  return *this;
}

BusyThreadCount::BusyThreadCount()
    : shards_(grpc_core::Clamp(gpr_cpu_num_cores(), 2u, 64u)) {}

BusyThreadCount::AutoThreadCounter BusyThreadCount::MakeAutoThreadCounter(
    size_t idx) {
  return AutoThreadCounter(this, idx);
};

void BusyThreadCount::Increment(size_t idx) {
  shards_[idx].busy_count.fetch_add(1, std::memory_order_relaxed);
}

void BusyThreadCount::Decrement(size_t idx) {
  shards_[idx].busy_count.fetch_sub(1, std::memory_order_relaxed);
}

size_t BusyThreadCount::count() {
  return std::accumulate(
      shards_.begin(), shards_.end(), 0, [](size_t running, ShardedData& d) {
        return running + d.busy_count.load(std::memory_order_relaxed);
      });
}

size_t BusyThreadCount::NextIndex() {
  return next_idx_.fetch_add(1) % shards_.size();
}

// -------- LivingThreadCount --------

LivingThreadCount::AutoThreadCounter::AutoThreadCounter(
    LivingThreadCount* counter)
    : counter_(counter) {
  counter_->Increment();
}

LivingThreadCount::AutoThreadCounter::~AutoThreadCounter() {
  if (counter_ != nullptr) counter_->Decrement();
}

LivingThreadCount::AutoThreadCounter::AutoThreadCounter(
    LivingThreadCount::AutoThreadCounter&& other) noexcept {
  counter_ = std::exchange(other.counter_, nullptr);
}

LivingThreadCount::AutoThreadCounter&
LivingThreadCount::AutoThreadCounter::operator=(
    LivingThreadCount::AutoThreadCounter&& other) noexcept {
  counter_ = std::exchange(other.counter_, nullptr);
  return *this;
}

LivingThreadCount::AutoThreadCounter
LivingThreadCount::MakeAutoThreadCounter() {
  return AutoThreadCounter(this);
};

void LivingThreadCount::Increment() {
  grpc_core::MutexLock lock(&mu_);
  ++living_count_;
  cv_.SignalAll();
}

void LivingThreadCount::Decrement() {
  grpc_core::MutexLock lock(&mu_);
  --living_count_;
  cv_.SignalAll();
}

void LivingThreadCount::BlockUntilThreadCount(size_t desired_threads,
                                              const char* why) {
  constexpr grpc_core::Duration log_rate = grpc_core::Duration::Seconds(3);
  while (true) {
    auto curr_threads = WaitForCountChange(desired_threads, log_rate);
    if (curr_threads == desired_threads) break;
    GRPC_LOG_EVERY_N_SEC_DELAYED(
        log_rate.seconds(), GPR_DEBUG,
        "Waiting for thread pool to idle before %s. (%" PRIdPTR " to %" PRIdPTR
        ")",
        why, curr_threads, desired_threads);
  }
}

size_t LivingThreadCount::count() {
  grpc_core::MutexLock lock(&mu_);
  return CountLocked();
}

size_t LivingThreadCount::CountLocked() { return living_count_; }

size_t LivingThreadCount::WaitForCountChange(size_t desired_threads,
                                             grpc_core::Duration timeout) {
  size_t count;
  auto deadline = absl::Now() + absl::Milliseconds(timeout.millis());
  do {
    grpc_core::MutexLock lock(&mu_);
    count = CountLocked();
    if (count == desired_threads) break;
    cv_.WaitWithDeadline(&mu_, deadline);
  } while (absl::Now() < deadline);
  return count;
}

}  // namespace experimental
}  // namespace grpc_event_engine
