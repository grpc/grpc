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

#include <random>
#include <thread>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/memory_quota.h"

namespace grpc_core {

namespace {
class StressTest {
 public:
  // Create a stress test with some size.
  StressTest(size_t num_quotas, size_t num_allocators) {
    for (size_t i = 0; i < num_quotas; ++i) {
      quotas_.emplace_back();
    }
    std::random_device g;
    std::uniform_int_distribution<size_t> dist(0, num_quotas - 1);
    for (size_t i = 0; i < num_allocators; ++i) {
      allocators_.emplace_back(quotas_[dist(g)].CreateMemoryOwner());
    }
  }

  // Run the thread for some period of time.
  void Run(int seconds) {
    std::vector<std::thread> threads;

    // A few threads constantly rebinding allocators to different quotas.
    threads.reserve(2 + 2 + 3 * allocators_.size());
    for (int i = 0; i < 2; i++) threads.push_back(Run(Rebinder));
    // And another few threads constantly resizing quotas.
    for (int i = 0; i < 2; i++) threads.push_back(Run(Resizer));

    // For each (allocator, pass), start a thread continuously allocating from
    // that allocator. Whenever the first allocation is made, schedule a
    // reclaimer for that pass.
    for (size_t i = 0; i < allocators_.size(); i++) {
      auto* allocator = &allocators_[i];
      for (ReclamationPass pass :
           {ReclamationPass::kBenign, ReclamationPass::kIdle,
            ReclamationPass::kDestructive}) {
        threads.push_back(Run([allocator, pass](StatePtr st) mutable {
          if (st->RememberReservation(allocator->allocator()->MakeReservation(
                  st->RandomRequest()))) {
            allocator->PostReclaimer(
                pass, [st](absl::optional<ReclamationSweep> sweep) {
                  if (!sweep.has_value()) return;
                  st->ForgetReservations();
                });
          }
        }));
      }
    }

    // All threads started, wait for the alloted time.
    std::this_thread::sleep_for(std::chrono::seconds(seconds));

    // Toggle the completion bit, and then wait for the threads.
    done_.store(true, std::memory_order_relaxed);
    while (!threads.empty()) {
      threads.back().join();
      threads.pop_back();
    }
  }

 private:
  // Per-thread state.
  // Not everything is used on every thread, but it's not terrible having the
  // extra state around and it does simplify things somewhat.
  class State {
   public:
    explicit State(StressTest* test)
        : test_(test),
          quotas_distribution_(0, test_->quotas_.size() - 1),
          allocators_distribution_(0, test_->allocators_.size() - 1),
          size_distribution_(1, 4 * 1024 * 1024),
          quota_size_distribution_(1024 * 1024, size_t(8) * 1024 * 1024 * 1024),
          choose_variable_size_(1, 100) {}

    // Choose a random quota, and return an owned pointer to it.
    // Not thread-safe, only callable from the owning thread.
    MemoryQuota* RandomQuota() {
      return &test_->quotas_[quotas_distribution_(g_)];
    }

    // Choose a random allocator, and return a borrowed pointer to it.
    // Not thread-safe, only callable from the owning thread.
    MemoryOwner* RandomAllocator() {
      return &test_->allocators_[allocators_distribution_(g_)];
    }

    // Random memory request size - 1% of allocations are chosen to be variable
    // sized - the rest are fixed (since variable sized create some contention
    // problems between allocator threads of different passes on the same
    // allocator).
    // Not thread-safe, only callable from the owning thread.
    MemoryRequest RandomRequest() {
      size_t a = size_distribution_(g_);
      if (choose_variable_size_(g_) == 1) {
        size_t b = size_distribution_(g_);
        return MemoryRequest(std::min(a, b), std::max(a, b));
      }
      return MemoryRequest(a);
    }

    // Choose a new size for a backing quota.
    // Not thread-safe, only callable from the owning thread.
    size_t RandomQuotaSize() { return quota_size_distribution_(g_); }

    // Remember a reservation, return true if it's the first remembered since
    // the last reclamation.
    // Thread-safe.
    bool RememberReservation(MemoryAllocator::Reservation reservation)
        ABSL_LOCKS_EXCLUDED(mu_) {
      MutexLock lock(&mu_);
      bool was_empty = reservations_.empty();
      reservations_.emplace_back(std::move(reservation));
      return was_empty;
    }

    // Return all reservations made until this moment, so that they can be
    // dropped.
    std::vector<MemoryAllocator::Reservation> ForgetReservations()
        ABSL_LOCKS_EXCLUDED(mu_) {
      MutexLock lock(&mu_);
      return std::move(reservations_);
    }

   private:
    // Owning test.
    StressTest* const test_;
    // Random number generator.
    std::mt19937 g_{std::random_device()()};
    // Distribution to choose a quota.
    std::uniform_int_distribution<size_t> quotas_distribution_;
    // Distribution to choose an allocator.
    std::uniform_int_distribution<size_t> allocators_distribution_;
    // Distribution to choose an allocation size.
    std::uniform_int_distribution<size_t> size_distribution_;
    // Distribution to choose a quota size.
    std::uniform_int_distribution<size_t> quota_size_distribution_;
    // Distribution to choose whether to make a variable-sized allocation.
    std::uniform_int_distribution<size_t> choose_variable_size_;

    // Mutex to protect the reservation list.
    Mutex mu_;
    // Reservations remembered by this thread.
    std::vector<MemoryAllocator::Reservation> reservations_
        ABSL_GUARDED_BY(mu_);
  };
  // Type alias since we always pass around these shared pointers.
  using StatePtr = std::shared_ptr<State>;

  // Choose one allocator, one quota, rebind the allocator to the quota.
  static void Rebinder(StatePtr st) {
    auto* allocator = st->RandomAllocator();
    auto* quota = st->RandomQuota();
    allocator->Rebind(quota);
  }

  // Choose one allocator, resize it to a randomly chosen size.
  static void Resizer(StatePtr st) {
    auto* quota = st->RandomQuota();
    size_t size = st->RandomQuotaSize();
    quota->SetSize(size);
  }

  // Create a thread that repeatedly runs a function until the test is done.
  // We create one instance of State that we pass as a StatePtr to said
  // function as the current overall state for this thread.
  // Monitors done_ to see when we should stop.
  // Ensures there's an ExecCtx for each iteration of the loop.
  template <typename Fn>
  std::thread Run(Fn fn) {
    return std::thread([this, fn]() mutable {
      auto state = std::make_shared<State>(this);
      while (!done_.load(std::memory_order_relaxed)) {
        ExecCtx exec_ctx;
        fn(state);
      }
    });
  }

  // Flag for when the test is completed.
  std::atomic<bool> done_{false};

  // Memory quotas to test against. We build this up at construction time, but
  // then don't resize, so we can load from it continuously from all of the
  // threads.
  std::vector<MemoryQuota> quotas_;
  // Memory allocators to test against. Similarly, built at construction time,
  // and then the shape of this vector is not changed.
  std::vector<MemoryOwner> allocators_;
};
}  // namespace

}  // namespace grpc_core

int main(int, char**) { grpc_core::StressTest(16, 64).Run(8); }
