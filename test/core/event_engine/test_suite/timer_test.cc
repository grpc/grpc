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

#include <chrono>
#include <random>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/functional/bind_front.h"
#include "absl/time/time.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/event_engine/test_suite/event_engine_test.h"

using ::testing::ElementsAre;
using namespace std::chrono_literals;

class EventEngineTimerTest : public EventEngineTest {
 public:
  void ScheduleCheckCB(absl::Time when, std::atomic<int>* call_count,
                       std::atomic<int>* fail_count, int total_expected);

 protected:
  void WaitForSignalled(absl::Duration timeout)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    absl::Time deadline = absl::Now() + timeout;
    while (!signaled_) {
      timeout = deadline - absl::Now();
      ASSERT_GT(timeout, absl::ZeroDuration());
      cv_.WaitWithTimeout(&mu_, timeout);
    }
  }

  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  bool signaled_ ABSL_GUARDED_BY(mu_) = false;
};

TEST_F(EventEngineTimerTest, ImmediateCallbackIsExecutedQuickly) {
  grpc_core::ExecCtx exec_ctx;
  auto engine = this->NewEventEngine();
  grpc_core::MutexLock lock(&mu_);
  engine->RunAfter(0ms, [this]() {
    grpc_core::MutexLock lock(&mu_);
    signaled_ = true;
    cv_.Signal();
  });
  WaitForSignalled(absl::Seconds(5));
}

TEST_F(EventEngineTimerTest, SupportsCancellation) {
  grpc_core::ExecCtx exec_ctx;
  auto engine = this->NewEventEngine();
  auto handle = engine->RunAfter(24h, []() {});
  ASSERT_TRUE(engine->Cancel(handle));
}

TEST_F(EventEngineTimerTest, CancelledCallbackIsNotExecuted) {
  grpc_core::ExecCtx exec_ctx;
  {
    auto engine = this->NewEventEngine();
    auto handle = engine->RunAfter(24h, [this]() {
      grpc_core::MutexLock lock(&mu_);
      signaled_ = true;
    });
    ASSERT_TRUE(engine->Cancel(handle));
  }
  // The engine is deleted, and all closures should have been flushed
  grpc_core::MutexLock lock(&mu_);
  ASSERT_FALSE(signaled_);
}

TEST_F(EventEngineTimerTest, TimersRespectScheduleOrdering) {
  grpc_core::ExecCtx exec_ctx;
  // Note: this is a brittle test if the first call to `RunAfter` takes longer
  // than the second callback's wait time.
  std::vector<uint8_t> ordered;
  uint8_t count = 0;
  grpc_core::MutexLock lock(&mu_);
  {
    auto engine = this->NewEventEngine();
    engine->RunAfter(100ms, [&]() {
      grpc_core::MutexLock lock(&mu_);
      ordered.push_back(2);
      ++count;
      cv_.Signal();
    });
    engine->RunAfter(0ms, [&]() {
      grpc_core::MutexLock lock(&mu_);
      ordered.push_back(1);
      ++count;
      cv_.Signal();
    });
    // Ensure both callbacks have run.
    while (count != 2) {
      cv_.WaitWithTimeout(&mu_, absl::Milliseconds(8));
    }
  }
  // The engine is deleted, and all closures should have been flushed beforehand
  ASSERT_THAT(ordered, ElementsAre(1, 2));
}

TEST_F(EventEngineTimerTest, CancellingExecutedCallbackIsNoopAndReturnsFalse) {
  grpc_core::ExecCtx exec_ctx;
  auto engine = this->NewEventEngine();
  grpc_core::MutexLock lock(&mu_);
  auto handle = engine->RunAfter(0ms, [this]() {
    grpc_core::MutexLock lock(&mu_);
    signaled_ = true;
    cv_.Signal();
  });
  WaitForSignalled(absl::Seconds(10));
  // The callback has run, and now we'll try to cancel it.
  ASSERT_FALSE(engine->Cancel(handle));
}

void EventEngineTimerTest::ScheduleCheckCB(absl::Time when,
                                           std::atomic<int>* call_count,
                                           std::atomic<int>* fail_count,
                                           int total_expected) {
  // TODO(hork): make the EventEngine the time source of truth! libuv supports
  // millis, absl::Time reports in nanos. This generic test will be hard-coded
  // to the lowest common denominator until EventEngines can compare relative
  // times with supported resolution.
  grpc_core::ExecCtx exec_ctx;
  auto now = absl::Now();
  EXPECT_LE(when, now);
  if (when > now) ++(*fail_count);
  if (++(*call_count) == total_expected) {
    grpc_core::MutexLock lock(&mu_);
    signaled_ = true;
    cv_.Signal();
  }
}

TEST_F(EventEngineTimerTest, StressTestTimersNotCalledBeforeScheduled) {
  grpc_core::ExecCtx exec_ctx;
  auto engine = this->NewEventEngine();
  constexpr int thread_count = 100;
  constexpr int call_count_per_thread = 100;
  constexpr float timeout_min_seconds = 1;
  constexpr float timeout_max_seconds = 10;
  std::atomic<int> call_count{0};
  std::atomic<int> failed_call_count{0};
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (int thread_n = 0; thread_n < thread_count; ++thread_n) {
    threads.emplace_back([&]() {
      grpc_core::ExecCtx exec_ctx;
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_real_distribution<> dis(timeout_min_seconds,
                                           timeout_max_seconds);
      for (int call_n = 0; call_n < call_count_per_thread; ++call_n) {
        const auto dur = static_cast<int64_t>(1e9 * dis(gen));
        auto deadline = absl::Now() + absl::Nanoseconds(dur);
        engine->RunAfter(
            std::chrono::nanoseconds(dur),
            absl::bind_front(&EventEngineTimerTest::ScheduleCheckCB, this,
                             deadline, &call_count, &failed_call_count,
                             thread_count * call_count_per_thread));
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  grpc_core::MutexLock lock(&mu_);
  // to protect against spurious wakeups.
  while (!signaled_) {
    cv_.Wait(&mu_);
  }
  gpr_log(GPR_DEBUG, "failed timer count: %d of %d", failed_call_count.load(),
          thread_count * call_count);
  ASSERT_EQ(0, failed_call_count.load());
}
