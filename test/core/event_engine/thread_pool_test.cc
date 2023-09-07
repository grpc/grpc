// Copyright 2022 The gRPC Authors
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

#include "src/core/lib/event_engine/thread_pool/thread_pool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <thread>
#include <vector>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/event_engine/thread_pool/original_thread_pool.h"
#include "src/core/lib/event_engine/thread_pool/thread_count.h"
#include "src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/test_config.h"

namespace grpc_event_engine {
namespace experimental {

template <typename T>
class ThreadPoolTest : public testing::Test {};

using ThreadPoolTypes =
    ::testing::Types<OriginalThreadPool, WorkStealingThreadPool>;
TYPED_TEST_SUITE(ThreadPoolTest, ThreadPoolTypes);

TYPED_TEST(ThreadPoolTest, CanRunAnyInvocable) {
  TypeParam p(8);
  grpc_core::Notification n;
  p.Run([&n] { n.Notify(); });
  n.WaitForNotification();
  p.Quiesce();
}

TYPED_TEST(ThreadPoolTest, CanDestroyInsideClosure) {
  auto* p = new TypeParam(8);
  grpc_core::Notification n;
  p->Run([p, &n]() mutable {
    // This should delete the thread pool and not deadlock
    p->Quiesce();
    delete p;
    n.Notify();
  });
  n.WaitForNotification();
}

TYPED_TEST(ThreadPoolTest, CanSurviveFork) {
  TypeParam p(8);
  grpc_core::Notification inner_closure_ran;
  p.Run([&inner_closure_ran, &p] {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    p.Run([&inner_closure_ran] {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      inner_closure_ran.Notify();
    });
  });
  // simulate a fork and watch the child process
  p.PrepareFork();
  p.PostforkChild();
  inner_closure_ran.WaitForNotification();
  grpc_core::Notification n2;
  p.Run([&n2] { n2.Notify(); });
  n2.WaitForNotification();
  p.Quiesce();
}

TYPED_TEST(ThreadPoolTest, ForkStressTest) {
  // Runs a large number of closures and multiple simulated fork events,
  // ensuring that only some fixed number of closures are executed between fork
  // events.
  //
  // Why: Python relies on fork support, and fork behaves poorly in the presence
  // of threads, but non-deterministically. gRPC has had problems in this space.
  // This test exercises a subset of the fork logic, the pieces we can control
  // without an actual OS fork.
  constexpr int expected_runcount = 1000;
  constexpr absl::Duration fork_freqency{absl::Milliseconds(50)};
  constexpr int num_closures_between_forks{100};
  TypeParam pool(8);
  std::atomic<int> runcount{0};
  std::atomic<int> fork_count{0};
  std::function<void()> inner_fn;
  inner_fn = [&]() {
    auto curr_runcount = runcount.load(std::memory_order_relaxed);
    // exit when the right number of closures have run, with some flex for
    // relaxed atomics.
    if (curr_runcount >= expected_runcount) return;
    if (fork_count.load(std::memory_order_relaxed) *
            num_closures_between_forks <=
        curr_runcount) {
      // skip incrementing, and schedule again.
      pool.Run(inner_fn);
      return;
    }
    runcount.fetch_add(1, std::memory_order_relaxed);
  };
  for (auto i = 0; i < expected_runcount; i++) {
    pool.Run(inner_fn);
  }
  // simulate multiple forks at a fixed frequency
  int curr_runcount = 0;
  while (curr_runcount < expected_runcount) {
    absl::SleepFor(fork_freqency);
    curr_runcount = runcount.load(std::memory_order_relaxed);
    int curr_forkcount = fork_count.load(std::memory_order_relaxed);
    if (curr_forkcount * num_closures_between_forks > curr_runcount) {
      continue;
    }
    pool.PrepareFork();
    pool.PostforkChild();
    fork_count.fetch_add(1);
  }
  ASSERT_GE(fork_count.load(), expected_runcount / num_closures_between_forks);
  // owners are the local pool, and the copy inside `inner_fn`.
  pool.Quiesce();
}

TYPED_TEST(ThreadPoolTest, StartQuiesceRaceStressTest) {
  // Repeatedly race Start and Quiesce against each other to ensure thread
  // safety.
  constexpr int iter_count = 500;
  struct ThdState {
    std::unique_ptr<TypeParam> pool;
    int i;
  };
  for (auto i = 0; i < iter_count; i++) {
    ThdState state{std::make_unique<TypeParam>(8), i};
    state.pool->PrepareFork();
    grpc_core::Thread t1(
        "t1",
        [](void* arg) {
          ThdState* state = static_cast<ThdState*>(arg);
          state->i % 2 == 0 ? state->pool->Quiesce()
                            : state->pool->PostforkParent();
        },
        &state, nullptr,
        grpc_core::Thread::Options().set_tracked(false).set_joinable(true));
    grpc_core::Thread t2(
        "t2",
        [](void* arg) {
          ThdState* state = static_cast<ThdState*>(arg);
          state->i % 2 == 1 ? state->pool->Quiesce()
                            : state->pool->PostforkParent();
        },
        &state, nullptr,
        grpc_core::Thread::Options().set_tracked(false).set_joinable(true));
    t1.Start();
    t2.Start();
    t1.Join();
    t2.Join();
  }
}

void ScheduleSelf(ThreadPool* p) {
  p->Run([p] { ScheduleSelf(p); });
}

void ScheduleTwiceUntilZero(ThreadPool* p, std::atomic<int>& runcount, int n) {
  runcount.fetch_add(1);
  if (n == 0) return;
  p->Run([p, &runcount, n] {
    ScheduleTwiceUntilZero(p, runcount, n - 1);
    ScheduleTwiceUntilZero(p, runcount, n - 1);
  });
}

TYPED_TEST(ThreadPoolTest, CanStartLotsOfClosures) {
  // TODO(hork): this is nerfed due to the original thread pool taking eons to
  // finish running 2M closures in some cases (usually < 10s, sometimes over
  // 90s). Reset the branch factor to 20 when all thread pool runtimes
  // stabilize.
  TypeParam p(8);
  std::atomic<int> runcount{0};
  // Our first thread pool implementation tried to create ~1M threads for this
  // test.
  int branch_factor = 18;
  ScheduleTwiceUntilZero(&p, runcount, branch_factor);
  p.Quiesce();
  ASSERT_EQ(runcount.load(), pow(2, branch_factor + 1) - 1);
}

class WorkStealingThreadPoolTest : public ::testing::Test {};

// TODO(hork): This is currently a pathological case for the original thread
// pool, it gets wedged in ~3% of runs when new threads fail to start. When that
// is fixed, or the implementation is deleted, make this a typed test again.
TEST_F(WorkStealingThreadPoolTest, ScalesWhenBackloggedFromGlobalQueue) {
  int pool_thread_count = 8;
  WorkStealingThreadPool p(pool_thread_count);
  grpc_core::Notification signal;
  // Ensures the pool is saturated before signaling closures to continue.
  std::atomic<int> waiters{0};
  std::atomic<bool> signaled{false};
  for (auto i = 0; i < pool_thread_count; i++) {
    p.Run([&]() {
      waiters.fetch_add(1);
      while (!signaled.load()) {
        signal.WaitForNotification();
      }
    });
  }
  while (waiters.load() != pool_thread_count) {
    absl::SleepFor(absl::Milliseconds(50));
  }
  p.Run([&]() {
    signaled.store(true);
    signal.Notify();
  });
  p.Quiesce();
}

// TODO(hork): This is currently a pathological case for the original thread
// pool, it gets wedged in ~3% of runs when new threads fail to start. When that
// is fixed, or the implementation is deleted, make this a typed test again.
TEST_F(WorkStealingThreadPoolTest,
       ScalesWhenBackloggedFromSingleThreadLocalQueue) {
  constexpr int pool_thread_count = 8;
  WorkStealingThreadPool p(pool_thread_count);
  grpc_core::Notification signal;
  // Ensures the pool is saturated before signaling closures to continue.
  std::atomic<int> waiters{0};
  std::atomic<bool> signaled{false};
  p.Run([&]() {
    for (int i = 0; i < pool_thread_count; i++) {
      p.Run([&]() {
        waiters.fetch_add(1);
        while (!signaled.load()) {
          signal.WaitForNotification();
        }
      });
    }
    while (waiters.load() != pool_thread_count) {
      absl::SleepFor(absl::Milliseconds(50));
    }
    p.Run([&]() {
      signaled.store(true);
      signal.Notify();
    });
  });
  p.Quiesce();
}

// TODO(hork): This is currently a pathological case for the original thread
// pool, it takes around 50s to run. When that is fixed, or the implementation
// is deleted, make this a typed test again.
TEST_F(WorkStealingThreadPoolTest, QuiesceRaceStressTest) {
  constexpr int cycle_count = 333;
  constexpr int thread_count = 8;
  constexpr int run_count = thread_count * 2;
  for (auto i = 0; i < cycle_count; i++) {
    WorkStealingThreadPool p(thread_count);
    for (auto j = 0; j < run_count; j++) {
      p.Run([]() {});
    }
    p.Quiesce();
  }
}

class BusyThreadCountTest : public testing::Test {};

TEST_F(BusyThreadCountTest, StressTest) {
  // Spawns a large number of threads to concurrently increments/decrement the
  // counters, and request count totals. Magic numbers were tuned for tests to
  // run in a reasonable amount of time.
  constexpr size_t thread_count = 300;
  constexpr int run_count = 1000;
  constexpr int increment_by = 50;
  BusyThreadCount busy_thread_count;
  grpc_core::Notification stop_counting;
  std::thread counter_thread([&]() {
    while (!stop_counting.HasBeenNotified()) {
      busy_thread_count.count();
    }
  });
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (size_t i = 0; i < thread_count; i++) {
    threads.emplace_back([&]() {
      for (int j = 0; j < run_count; j++) {
        // Get a new index for every iteration.
        // This is not the intended use, but further stress tests the NextIndex
        // function.
        auto thread_idx = busy_thread_count.NextIndex();
        for (int inc = 0; inc < increment_by; inc++) {
          busy_thread_count.Increment(thread_idx);
        }
        for (int inc = 0; inc < increment_by; inc++) {
          busy_thread_count.Decrement(thread_idx);
        }
      }
    });
  }
  for (auto& thd : threads) thd.join();
  stop_counting.Notify();
  counter_thread.join();
  ASSERT_EQ(busy_thread_count.count(), 0);
}

TEST_F(BusyThreadCountTest, AutoCountStressTest) {
  // Spawns a large number of threads to concurrently increments/decrement the
  // counters, and request count totals. Magic numbers were tuned for tests to
  // run in a reasonable amount of time.
  constexpr size_t thread_count = 150;
  constexpr int run_count = 1000;
  constexpr int increment_by = 30;
  BusyThreadCount busy_thread_count;
  grpc_core::Notification stop_counting;
  std::thread counter_thread([&]() {
    while (!stop_counting.HasBeenNotified()) {
      busy_thread_count.count();
    }
  });
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (size_t i = 0; i < thread_count; i++) {
    threads.emplace_back([&]() {
      for (int j = 0; j < run_count; j++) {
        std::vector<BusyThreadCount::AutoThreadCounter> auto_counters;
        auto_counters.reserve(increment_by);
        for (int ctr_count = 0; ctr_count < increment_by; ctr_count++) {
          auto_counters.push_back(busy_thread_count.MakeAutoThreadCounter(
              busy_thread_count.NextIndex()));
        }
      }
    });
  }
  for (auto& thd : threads) thd.join();
  stop_counting.Notify();
  counter_thread.join();
  ASSERT_EQ(busy_thread_count.count(), 0);
}

class LivingThreadCountTest : public testing::Test {};

TEST_F(LivingThreadCountTest, StressTest) {
  // Spawns a large number of threads to concurrently increments/decrement the
  // counters, and request count totals. Magic numbers were tuned for tests to
  // run in a reasonable amount of time.
  constexpr size_t thread_count = 50;
  constexpr int run_count = 1000;
  constexpr int increment_by = 10;
  LivingThreadCount living_thread_count;
  grpc_core::Notification stop_counting;
  std::thread counter_thread([&]() {
    while (!stop_counting.HasBeenNotified()) {
      living_thread_count.count();
    }
  });
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (size_t i = 0; i < thread_count; i++) {
    threads.emplace_back([&]() {
      for (int j = 0; j < run_count; j++) {
        // Get a new index for every iteration.
        // This is not the intended use, but further stress tests the NextIndex
        // function.
        for (int inc = 0; inc < increment_by; inc++) {
          living_thread_count.Increment();
        }
        for (int inc = 0; inc < increment_by; inc++) {
          living_thread_count.Decrement();
        }
      }
    });
  }
  for (auto& thd : threads) thd.join();
  stop_counting.Notify();
  counter_thread.join();
  ASSERT_EQ(living_thread_count.count(), 0);
}

TEST_F(LivingThreadCountTest, AutoCountStressTest) {
  // Spawns a large number of threads to concurrently increments/decrement the
  // counters, and request count totals. Magic numbers were tuned for tests to
  // run in a reasonable amount of time.
  constexpr size_t thread_count = 50;
  constexpr int run_count = 1000;
  constexpr int increment_by = 10;
  LivingThreadCount living_thread_count;
  grpc_core::Notification stop_counting;
  std::thread counter_thread([&]() {
    while (!stop_counting.HasBeenNotified()) {
      living_thread_count.count();
    }
  });
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (size_t i = 0; i < thread_count; i++) {
    threads.emplace_back([&]() {
      for (int j = 0; j < run_count; j++) {
        std::vector<LivingThreadCount::AutoThreadCounter> auto_counters;
        auto_counters.reserve(increment_by);
        for (int ctr_count = 0; ctr_count < increment_by; ctr_count++) {
          auto_counters.push_back(living_thread_count.MakeAutoThreadCounter());
        }
      }
    });
  }
  for (auto& thd : threads) thd.join();
  stop_counting.Notify();
  counter_thread.join();
  ASSERT_EQ(living_thread_count.count(), 0);
}

TEST_F(LivingThreadCountTest, BlockUntilThreadCountTest) {
  constexpr size_t thread_count = 100;
  grpc_core::Notification waiting;
  LivingThreadCount living_thread_count;
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  // Start N living threads
  for (size_t i = 0; i < thread_count; i++) {
    threads.emplace_back([&]() {
      auto alive = living_thread_count.MakeAutoThreadCounter();
      waiting.WaitForNotification();
    });
  }
  // Join in a separate thread
  std::thread joiner([&]() {
    waiting.Notify();
    for (auto& thd : threads) thd.join();
  });
  {
    auto alive = living_thread_count.MakeAutoThreadCounter();
    living_thread_count.BlockUntilThreadCount(1,
                                              "block until 1 thread remains");
  }
  living_thread_count.BlockUntilThreadCount(0,
                                            "block until all threads are gone");
  joiner.join();
  ASSERT_EQ(living_thread_count.count(), 0);
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
