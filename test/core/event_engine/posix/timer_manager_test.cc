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

#include "src/core/lib/event_engine/posix_engine/timer_manager.h"

#include <atomic>
#include <memory>
#include <random>

#include "absl/functional/any_invocable.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

namespace grpc_event_engine {
namespace experimental {

TEST(TimerManagerTest, StressTest) {
  grpc_core::ExecCtx exec_ctx;
  auto now = grpc_core::Timestamp::Now();
  auto test_deadline = now + grpc_core::Duration::Seconds(15);
  std::vector<Timer> timers;
  constexpr int kTimerCount = 500;
  timers.resize(kTimerCount);
  std::atomic_int called{0};
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> dis_millis(100, 3000);
  auto pool = MakeThreadPool(8);
  {
    TimerManager manager(pool);
    for (auto& timer : timers) {
      exec_ctx.InvalidateNow();
      manager.TimerInit(
          &timer, now + grpc_core::Duration::Milliseconds(dis_millis(gen)),
          experimental::SelfDeletingClosure::Create([&called]() {
            absl::SleepFor(absl::Milliseconds(50));
            ++called;
          }));
    }
    // Wait for all callbacks to have been called
    while (called.load(std::memory_order_relaxed) < kTimerCount) {
      exec_ctx.InvalidateNow();
      if (grpc_core::Timestamp::Now() > test_deadline) {
        FAIL() << "Deadline exceeded. "
               << called.load(std::memory_order_relaxed) << "/" << kTimerCount
               << " callbacks executed";
      }
      gpr_log(GPR_DEBUG, "Processed %d/%d callbacks",
              called.load(std::memory_order_relaxed), kTimerCount);
      absl::SleepFor(absl::Milliseconds(333));
    }
  }
  pool->Quiesce();
}

TEST(TimerManagerTest, ShutDownBeforeAllCallbacksAreExecuted) {
  // Should the internal timer_list complain in this scenario?
  grpc_core::ExecCtx exec_ctx;
  std::vector<Timer> timers;
  constexpr int kTimerCount = 100;
  timers.resize(kTimerCount);
  std::atomic_int called{0};
  experimental::AnyInvocableClosure closure([&called] { ++called; });
  auto pool = MakeThreadPool(8);
  {
    TimerManager manager(pool);
    for (auto& timer : timers) {
      manager.TimerInit(&timer, grpc_core::Timestamp::InfFuture(), &closure);
    }
  }
  ASSERT_EQ(called.load(), 0);
  pool->Quiesce();
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
