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

#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <thread>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/event_engine/thread_pool/original_thread_pool.h"
#include "src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.h"
#include "src/core/lib/gprpp/notification.h"
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
  for (int i = 0; i < expected_runcount; i++) {
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
  TypeParam p(8);
  std::atomic<int> runcount{0};
  // Our first thread pool implementation tried to create ~1M threads for this
  // test.
  ScheduleTwiceUntilZero(&p, runcount, 20);
  p.Quiesce();
  ASSERT_EQ(runcount.load(), pow(2, 21) - 1);
}

TYPED_TEST(ThreadPoolTest, ScalesWhenBackloggedFromSingleThreadLocalQueue) {
  int pool_thread_count = 8;
  TypeParam p(pool_thread_count);
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

TYPED_TEST(ThreadPoolTest, ScalesWhenBackloggedFromGlobalQueue) {
  int pool_thread_count = 8;
  TypeParam p(pool_thread_count);
  grpc_core::Notification signal;
  // Ensures the pool is saturated before signaling closures to continue.
  std::atomic<int> waiters{0};
  std::atomic<bool> signaled{false};
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
  p.Quiesce();
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
