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

#include "src/core/ext/filters/channel_idle/idle_filter_state.h"

#include <chrono>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(IdleFilterStateTest, IdlenessStartsTimer) {
  IdleFilterState s(false);
  EXPECT_TRUE(s.IncreaseCallCount());
  // First idle should start the timer
  EXPECT_TRUE(s.DecreaseCallCount());
  for (int i = 0; i < 10; i++) {
    // Next idle should not!
    EXPECT_TRUE(s.IncreaseCallCount());
    EXPECT_FALSE(s.DecreaseCallCount());
  }
}

TEST(IdleFilterStateTest, TimerStopsAfterIdle) {
  IdleFilterState s(true);
  EXPECT_FALSE(s.CheckTimer());
}

TEST(IdleFilterStateTest, TimerKeepsGoingWithActivity) {
  IdleFilterState s(true);
  for (int i = 0; i < 10; i++) {
    EXPECT_TRUE(s.IncreaseCallCount());
    (void)s.DecreaseCallCount();
    EXPECT_TRUE(s.CheckTimer());
  }
  EXPECT_FALSE(s.CheckTimer());
  EXPECT_FALSE(s.IncreaseCallCount());
}

TEST(IdleFilterStateTest, StressTest) {
  IdleFilterState s(false);
  std::atomic<bool> done{false};
  int idle_polls = 0;
  int thread_jumps = 0;
  std::vector<std::thread> threads;
  for (int idx = 0; idx < 10; idx++) {
    std::thread t([&] {
      int ctr = 0;
      auto increase = [&] {
        if (s.IncreaseCallCount()) {
          ctr++;
        } else {
          done.store(true, std::memory_order_relaxed);
        }
      };
      auto decrease = [&] {
        if (ctr == 0) return;
        ctr--;
        if (s.DecreaseCallCount()) {
          thread_jumps++;
          if (thread_jumps == 10) done.store(true, std::memory_order_relaxed);
          EXPECT_EQ(ctr, 0);
          do {
            idle_polls++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          } while (s.CheckTimer());
        }
      };
      std::mt19937 g{std::random_device()()};
      while (!done.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        for (int i = 0; i < 100; i++) {
          if (g() & 1) {
            increase();
          } else if (ctr > 0) {
            decrease();
          }
        }
        while (ctr > 0) {
          decrease();
        }
      }
      while (ctr > 0) {
        decrease();
      }
    });
    threads.emplace_back(std::move(t));
  }
  for (auto& thread : threads) thread.join();
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
