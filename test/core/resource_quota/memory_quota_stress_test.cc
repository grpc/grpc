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

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/memory_request.h>
#include <stddef.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <initializer_list>
#include <memory>
#include <optional>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/util/sync.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {

namespace {
class StressTest {
 public:
  // Create a stress test with some size.
  StressTest(size_t num_quotas, size_t num_allocators) {
    for (size_t i = 0; i < num_quotas; ++i) {
      quotas_.emplace_back(absl::StrCat("quota[", i, "]"));
    }
    std::random_device g;
    std::uniform_int_distribution<size_t> dist(0, num_quotas - 1);
    for (size_t i = 0; i < num_allocators; ++i) {
      allocators_.emplace_back(quotas_[dist(g)].CreateMemoryOwner());
    }
  }

  ~StressTest() {
    ExecCtx exec_ctx;
    allocators_.clear();
    quotas_.clear();
  }

  // Run the thread for some period of time.
  void Run(int seconds) {
    std::vector<std::thread> threads;

    // And another few threads constantly resizing quotas.
    threads.reserve(2 + allocators_.size());
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
          if (st->RememberReservation(
                  allocator->MakeReservation(st->RandomRequest()))) {
            allocator->PostReclaimer(
                pass, [st](std::optional<ReclamationSweep> sweep) {
                  if (!sweep.has_value()) return;
                  st->ForgetReservations();
                });
          }
        }));
      }
    }

    // All threads started, wait for the allotted time.
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
          quota_size_distribution_(1024 * 1024, size_t{8} * 1024 * 1024 * 1024),
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

TEST(MemoryQuotaStressTest, MainTest) {
  if (sizeof(void*) != 8) {
    LOG(ERROR) << "This test assumes 64-bit processors in the values it uses "
                  "for sizes. Since this test is mostly aimed at TSAN "
                  "coverage, and that's mostly platform independent, we simply "
                  "skip this test in 32-bit builds.";
    GTEST_SKIP();
  }
  grpc_core::StressTest(16, 20).Run(8);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment give_me_a_name(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
