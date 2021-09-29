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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/time/time.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
#include "test/core/event_engine/test_suite/event_engine_test.h"

using ::testing::ElementsAre;

class EventEngineTimerTest : public EventEngineTest {
 protected:
  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  bool signaled ABSL_GUARDED_BY(mu_) = false;
};

TEST_F(EventEngineTimerTest, ImmediateCallbackIsExecutedQuickly) {
  auto engine = this->NewEventEngine();
  grpc_core::MutexLock lock(&mu_);
  engine->RunAt(absl::Now(), [this]() {
    grpc_core::MutexLock lock(&mu_);
    signaled = true;
    cv_.Signal();
  });
  cv_.WaitWithTimeout(&mu_, absl::Seconds(5));
  ASSERT_TRUE(signaled);
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
      signaled = true;
    });
    ASSERT_TRUE(engine->Cancel(handle));
  }
  // The engine is deleted, and all closures should have been flushed
  grpc_core::MutexLock lock(&mu_);
  ASSERT_FALSE(signaled);
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
      count++;
      cv_.Signal();
    });
    engine->RunAt(absl::Now(), [&]() {
      grpc_core::MutexLock lock(&mu_);
      ordered.push_back(1);
      count++;
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
    signaled = true;
    cv_.Signal();
  });
  cv_.WaitWithTimeout(&mu_, absl::Seconds(10));
  ASSERT_TRUE(signaled);
  // The callback has run, and now we'll try to cancel it.
  ASSERT_FALSE(engine->Cancel(handle));
}
