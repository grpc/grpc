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

#include <grpc/grpc.h>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

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
      VLOG(2) << "Processed " << called.load(std::memory_order_relaxed) << "/"
              << kTimerCount << " callbacks";
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

#ifdef GRPC_POSIX_SOCKET_TCP

using namespace std::chrono_literals;

class PosixEventEngineTimerTest : public ::testing::Test {
 protected:
  static bool SameShard(PosixEventEngine& engine, EventEngine::TaskHandle a,
                        EventEngine::TaskHandle b) {
    return &engine.TimerShardForHandle(a) == &engine.TimerShardForHandle(b);
  }
};

TEST_F(PosixEventEngineTimerTest, CancellationDoesNotSerializeAllTimers) {
  std::promise<void> deleting;
  auto deleting_future = deleting.get_future();
  std::promise<void> release;
  auto released = release.get_future().share();
  auto engine = PosixEventEngine::MakePosixEventEngine();
  // Cancelling this timer blocks while destroying its captured state under
  // the handle lock. Other shards must still be able to cancel their timers.
  auto state = std::shared_ptr<int>(new int(0), [&](int* value) {
    deleting.set_value();
    released.wait();
    delete value;
  });
  auto blocked = engine->RunAfter(24h, [state = std::move(state)] {});
  std::vector<EventEngine::TaskHandle> handles;
  EventEngine::TaskHandle independent = EventEngine::TaskHandle::kInvalid;
  // Retain candidates so allocator reuse cannot keep selecting the same shard.
  for (int i = 0; i < 1024; ++i) {
    handles.push_back(engine->RunAfter(24h, [] {}));
    if (!SameShard(*engine, blocked, handles.back())) {
      independent = handles.back();
      break;
    }
  }
  if (independent == EventEngine::TaskHandle::kInvalid) {
    release.set_value();
    FAIL() << "Could not allocate timers on distinct shards";
  }
  std::thread blocker([&] { EXPECT_TRUE(engine->Cancel(blocked)); });
  EXPECT_EQ(deleting_future.wait_for(10s), std::future_status::ready);
  auto cancelled = std::async(std::launch::async,
                              [&] { return engine->Cancel(independent); });
  // A timer on another shard must be cancellable while deletion is blocked.
  EXPECT_EQ(cancelled.wait_for(10s), std::future_status::ready);
  release.set_value();
  blocker.join();
  EXPECT_TRUE(cancelled.get());
  for (auto handle : handles) {
    if (handle != independent) EXPECT_TRUE(engine->Cancel(handle));
  }
}

TEST_F(PosixEventEngineTimerTest, ConcurrentRegistrationAndStaleCancellation) {
  auto engine = PosixEventEngine::MakePosixEventEngine();
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 256; ++i) {
        auto handle = engine->RunAfter(24h, [] { ADD_FAILURE(); });
        EXPECT_TRUE(engine->Cancel(handle));
        // Allocator reuse must not let a stale handle cancel a newer timer.
        auto replacement = engine->RunAfter(24h, [] { ADD_FAILURE(); });
        EXPECT_FALSE(engine->Cancel(handle));
        // Exercise address reuse even if the allocator chose another address.
        auto stale_at_replacement_address = replacement;
        stale_at_replacement_address.keys[1] = handle.keys[1];
        EXPECT_FALSE(engine->Cancel(stale_at_replacement_address));
        EXPECT_TRUE(engine->Cancel(replacement));
      }
    });
  }
  for (auto& thread : threads) thread.join();
  EXPECT_FALSE(engine->Cancel(EventEngine::TaskHandle::kInvalid));
}

TEST_F(PosixEventEngineTimerTest, ConcurrentExpirationAndCancellation) {
  constexpr int kThreads = 8;
  constexpr int kPerThread = 128;
  struct Result {
    std::atomic<int> calls{0};
    bool cancelled = false;
  };
  std::array<Result, kThreads * kPerThread> results;
  std::atomic<int> completed{0};
  std::promise<void> started;
  auto started_future = started.get_future();
  std::promise<void> release;
  auto released = release.get_future().share();
  auto engine = PosixEventEngine::MakePosixEventEngine();
  // Guarantee coverage of cancellation during an executing callback, in
  // addition to the expiration/cancellation races below.
  auto running = engine->RunAfter(1ms, [&] {
    started.set_value();
    released.wait();
  });
  EXPECT_EQ(started_future.wait_for(10s), std::future_status::ready);
  EXPECT_FALSE(engine->Cancel(running));
  EXPECT_FALSE(engine->Cancel(running));
  release.set_value();
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < kPerThread; ++i) {
        auto& result = results[t * kPerThread + i];
        auto handle = engine->RunAfter(1ms, [&result, &completed] {
          ++result.calls;
          ++completed;
        });
        if (i % 2 == 0) std::this_thread::sleep_for(2ms);
        result.cancelled = engine->Cancel(handle);
        if (result.cancelled) ++completed;
        EXPECT_FALSE(engine->Cancel(handle));
      }
    });
  }
  for (auto& thread : threads) thread.join();
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (completed.load() < results.size() &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  // Quiesce callbacks before inspecting (or destroying) their captured state.
  engine.reset();
  EXPECT_EQ(completed.load(), results.size());
  for (const auto& result : results) {
    EXPECT_EQ(result.calls.load(), result.cancelled ? 0 : 1);
  }
}

TEST_F(PosixEventEngineTimerTest, CancelAllRacesWithRegistration) {
  constexpr int kThreads = 8;
  constexpr int kPerThread = 256;
  std::array<std::weak_ptr<int>, kThreads * kPerThread> states;
  std::array<EventEngine::TaskHandle, kThreads * kPerThread> handles;
  std::promise<void> draining;
  auto draining_future = draining.get_future();
  std::promise<void> release;
  auto released = release.get_future().share();
  auto engine = PosixEventEngine::MakePosixEventEngine();
  // Pause shutdown inside timer destruction, then bring the registration
  // threads to their first RunAfter call before allowing shutdown to proceed.
  auto blocker = std::shared_ptr<int>(new int(0), [&](int* value) {
    draining.set_value();
    released.wait();
    delete value;
  });
  engine->RunAfter(24h, [blocker = std::move(blocker)] { ADD_FAILURE(); });
  std::thread shutdown([&] { engine->CancelAllPendingTimers(); });
  EXPECT_EQ(draining_future.wait_for(10s), std::future_status::ready);
  std::array<std::promise<void>, kThreads> registering;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < kPerThread; ++i) {
        const int index = t * kPerThread + i;
        auto state = std::make_shared<int>(index);
        states[index] = state;
        if (i == 0) registering[t].set_value();
        handles[index] = engine->RunAfter(
            24h, [state = std::move(state)] { ADD_FAILURE(); });
      }
    });
  }
  for (auto& ready : registering) {
    EXPECT_EQ(ready.get_future().wait_for(10s), std::future_status::ready);
  }
  release.set_value();
  shutdown.join();
  for (auto& thread : threads) thread.join();
  for (int i = 0; i < states.size(); ++i) {
    EXPECT_TRUE(states[i].expired());
    EXPECT_FALSE(engine->Cancel(handles[i]));
  }
  // Registration after all shards have been closed must destroy the closure.
  auto state = std::make_shared<int>(0);
  std::weak_ptr<int> weak = state;
  auto handle =
      engine->RunAfter(24h, [state = std::move(state)] { ADD_FAILURE(); });
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(engine->Cancel(handle));
  engine->CancelAllPendingTimers();
}

TEST_F(PosixEventEngineTimerTest, DestructionDrainsEveryShard) {
  std::vector<std::weak_ptr<int>> states;
  auto engine = PosixEventEngine::MakePosixEventEngine();
  for (int i = 0; i < 1024; ++i) {
    auto state = std::make_shared<int>(i);
    states.push_back(state);
    engine->RunAfter(24h, [state = std::move(state)] { ADD_FAILURE(); });
  }
  engine.reset();
  for (const auto& state : states) EXPECT_TRUE(state.expired());
}

#endif  // GRPC_POSIX_SOCKET_TCP

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
