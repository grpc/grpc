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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/functional/bind_front.h"
#include "absl/time/time.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
#include "test/core/event_engine/test_suite/event_engine_test.h"

using ::testing::ElementsAre;

class EventEngineTimerTest : public EventEngineTest {
 public:
  void ScheduleCheckCB(absl::Time when, std::atomic<int>* call_count,
                       std::atomic<int>* fail_count, int total_expected);

 protected:
  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  bool signaled_ ABSL_GUARDED_BY(mu_) = false;
};

TEST_F(EventEngineTimerTest, ImmediateCallbackIsExecutedQuickly) {
  auto engine = this->NewEventEngine();
  grpc_core::MutexLock lock(&mu_);
  engine->RunAt(absl::Now(), [this]() {
    grpc_core::MutexLock lock(&mu_);
    signaled_ = true;
    cv_.Signal();
  });
  cv_.WaitWithTimeout(&mu_, absl::Seconds(5));
  ASSERT_TRUE(signaled_);
}

TEST_F(EventEngineTimerTest, SupportsCancellation) {
  auto engine = this->NewEventEngine();
  auto handle = engine->RunAt(absl::InfiniteFuture(), []() {});
  ASSERT_TRUE(engine->Cancel(handle));
}

TEST_F(EventEngineTimerTest, CancelledCallbackIsNotExecuted) {
  {
    auto engine = this->NewEventEngine();
    auto handle = engine->RunAt(absl::InfiniteFuture(), [this]() {
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
  // Note: this is a brittle test if the first call to `RunAt` takes longer than
  // the second callback's wait time.
  std::vector<uint8_t> ordered;
  uint8_t count = 0;
  grpc_core::MutexLock lock(&mu_);
  {
    auto engine = this->NewEventEngine();
    engine->RunAt(absl::Now() + absl::Seconds(1), [&]() {
      grpc_core::MutexLock lock(&mu_);
      ordered.push_back(2);
      ++count;
      cv_.Signal();
    });
    engine->RunAt(absl::Now(), [&]() {
      grpc_core::MutexLock lock(&mu_);
      ordered.push_back(1);
      ++count;
      cv_.Signal();
    });
    // Ensure both callbacks have run. Simpler than a mutex.
    while (count != 2) {
      cv_.WaitWithTimeout(&mu_, absl::Microseconds(100));
    }
  }
  // The engine is deleted, and all closures should have been flushed beforehand
  ASSERT_THAT(ordered, ElementsAre(1, 2));
}

TEST_F(EventEngineTimerTest, CancellingExecutedCallbackIsNoopAndReturnsFalse) {
  auto engine = this->NewEventEngine();
  grpc_core::MutexLock lock(&mu_);
  auto handle = engine->RunAt(absl::Now(), [this]() {
    grpc_core::MutexLock lock(&mu_);
    signaled_ = true;
    cv_.Signal();
  });
  cv_.WaitWithTimeout(&mu_, absl::Seconds(10));
  ASSERT_TRUE(signaled_);
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
  int64_t now_millis = absl::ToUnixMillis(absl::Now());
  int64_t when_millis = absl::ToUnixMillis(when);
  EXPECT_LE(when_millis, now_millis);
  if (when_millis > now_millis) ++(*fail_count);
  if (++(*call_count) == total_expected) {
    grpc_core::MutexLock lock(&mu_);
    signaled_ = true;
    cv_.Signal();
  }
}

TEST_F(EventEngineTimerTest, StressTestTimersNotCalledBeforeScheduled) {
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
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_real_distribution<> dis(timeout_min_seconds,
                                           timeout_max_seconds);
      for (int call_n = 0; call_n < call_count_per_thread; ++call_n) {
        absl::Time when = absl::Now() + absl::Seconds(dis(gen));
        engine->RunAt(
            when, absl::bind_front(&EventEngineTimerTest::ScheduleCheckCB, this,
                                   when, &call_count, &failed_call_count,
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
