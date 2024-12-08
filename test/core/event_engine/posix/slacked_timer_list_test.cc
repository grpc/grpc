//
//
// Copyright 2024 gRPC authors.
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
//
//

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/time.h>

#include <vector>

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/util/time.h"

using testing::Mock;
using testing::Return;
using testing::StrictMock;

namespace grpc_event_engine {
namespace experimental {

namespace {

class MockClosure : public experimental::EventEngine::Closure {
 public:
  MOCK_METHOD(void, Run, ());
};

class MockHost : public TimerListHost {
 public:
  ~MockHost() override {}
  MOCK_METHOD(grpc_core::Timestamp, Now, ());
  MOCK_METHOD(void, Kick, ());
};

enum class CheckResult { kTimersFired, kCheckedAndEmpty, kNotChecked };

CheckResult FinishCheck(
    absl::optional<std::vector<experimental::EventEngine::Closure*>> result) {
  if (!result.has_value()) return CheckResult::kNotChecked;
  if (result->empty()) return CheckResult::kCheckedAndEmpty;
  for (auto closure : *result) {
    closure->Run();
  }
  return CheckResult::kTimersFired;
}

}  // namespace

TEST(SlackedTimerListTest, Add) {
  Timer timers[20];
  StrictMock<MockClosure> closures[20];

  const auto kStart =
      grpc_core::Timestamp::FromMillisecondsAfterProcessEpoch(0);

  StrictMock<MockHost> host;
  SlackedTimerList::Options options;
  options.num_shards = 5;
  options.resolution = grpc_core::Duration::Minutes(1);
  SlackedTimerList timer_list(&host, options);

  // 10 ms timers.  will expire in the current epoch
  for (int i = 0; i < 10; i++) {
    timer_list.TimerInit(&timers[i],
                         kStart + grpc_core::Duration::Milliseconds(10),
                         &closures[i]);
  }

  // 1 min + 10ms timers.  will expire in the next epoch
  for (int i = 10; i < 15; i++) {
    timer_list.TimerInit(&timers[i],
                         kStart + grpc_core::Duration::Minutes(1) +
                             grpc_core::Duration::Milliseconds(10),
                         &closures[i]);
  }

  // 1 min + 31s timers.  will expire 2 epochs from now
  for (int i = 15; i < 20; i++) {
    timer_list.TimerInit(&timers[i],
                         kStart + grpc_core::Duration::Minutes(1) +
                             grpc_core::Duration::Seconds(31),
                         &closures[i]);
  }

  // Advance time by 500ms.  Only the first batch should be ready.
  EXPECT_CALL(host, Now())
      .WillOnce(Return(kStart + grpc_core::Duration::Milliseconds(500)));
  for (int i = 0; i < 10; i++) {
    EXPECT_CALL(closures[i], Run());
  }
  EXPECT_EQ(FinishCheck(timer_list.TimerCheck(nullptr)),
            CheckResult::kTimersFired);
  for (int i = 0; i < 10; i++) {
    Mock::VerifyAndClearExpectations(&closures[i]);
  }

  // 600ms later, no new timers should be ready
  EXPECT_CALL(host, Now())
      .WillOnce(Return(kStart + grpc_core::Duration::Milliseconds(600)));
  EXPECT_EQ(FinishCheck(timer_list.TimerCheck(nullptr)),
            CheckResult::kCheckedAndEmpty);

  // After 1min 29s mins the next batch should be ready
  EXPECT_CALL(host, Now())
      .WillOnce(Return(kStart + grpc_core::Duration::Minutes(1) +
                       grpc_core::Duration::Seconds(29)));
  for (int i = 10; i < 15; i++) {
    EXPECT_CALL(closures[i], Run());
  }
  EXPECT_EQ(FinishCheck(timer_list.TimerCheck(nullptr)),
            CheckResult::kTimersFired);
  for (int i = 10; i < 15; i++) {
    Mock::VerifyAndClearExpectations(&closures[i]);
  }

  // After 2 mins the next batch should be ready
  EXPECT_CALL(host, Now())
      .WillOnce(Return(kStart + grpc_core::Duration::Minutes(2)));
  for (int i = 15; i < 20; i++) {
    EXPECT_CALL(closures[i], Run());
  }
  EXPECT_EQ(FinishCheck(timer_list.TimerCheck(nullptr)),
            CheckResult::kTimersFired);
  for (int i = 15; i < 20; i++) {
    Mock::VerifyAndClearExpectations(&closures[i]);
  }

  // After 3 mins, no new timers should be ready
  EXPECT_CALL(host, Now())
      .WillOnce(Return(kStart + grpc_core::Duration::Minutes(3)));
  EXPECT_EQ(FinishCheck(timer_list.TimerCheck(nullptr)),
            CheckResult::kCheckedAndEmpty);
}

// Tests cancellation of timers.
TEST(SlackedTimerListTest, TimerCancellation) {
  Timer timers[5];
  StrictMock<MockClosure> closures[5];

  const auto kStart =
      grpc_core::Timestamp::FromMillisecondsAfterProcessEpoch(0);

  StrictMock<MockHost> host;
  SlackedTimerList::Options options;
  options.num_shards = 5;
  options.resolution = grpc_core::Duration::Minutes(1);
  SlackedTimerList timer_list(&host, options);

  // Timer-1 with deadline 100ms in the future
  timer_list.TimerInit(&timers[0],
                       kStart + grpc_core::Duration::Milliseconds(100),
                       &closures[0]);

  // Timer-2 with deadline 1 min + 10ms in the future
  timer_list.TimerInit(&timers[1],
                       kStart + grpc_core::Duration::Minutes(1) +
                           grpc_core::Duration::Milliseconds(10),
                       &closures[1]);

  // Timer-3 with deadline 400ms in the future
  timer_list.TimerInit(&timers[2],
                       kStart + grpc_core::Duration::Milliseconds(400),
                       &closures[2]);

  // Timer-4 with deadline 1 min + 30s in the future
  timer_list.TimerInit(&timers[3],
                       kStart + grpc_core::Duration::Minutes(1) +
                           grpc_core::Duration::Seconds(30),
                       &closures[3]);

  // Timer-5 with deadline 2 mins + 10ms in the future
  timer_list.TimerInit(&timers[4],
                       kStart + grpc_core::Duration::Minutes(2) +
                           grpc_core::Duration::Milliseconds(10),
                       &closures[4]);

  // Advance time by 1 min + 10ms
  EXPECT_CALL(host, Now())
      .WillOnce(Return(kStart + grpc_core::Duration::Minutes(1) +
                       grpc_core::Duration::Milliseconds(10)));
  // Timers 1, 2 & 3 should have run.
  EXPECT_CALL(closures[0], Run());
  EXPECT_CALL(closures[1], Run());
  EXPECT_CALL(closures[2], Run());
  EXPECT_EQ(FinishCheck(timer_list.TimerCheck(nullptr)),
            CheckResult::kTimersFired);
  Mock::VerifyAndClearExpectations(&closures[0]);
  Mock::VerifyAndClearExpectations(&closures[1]);
  Mock::VerifyAndClearExpectations(&closures[2]);
  // Timers 1, 2 & 3 should not be cancellable.
  EXPECT_FALSE(timer_list.TimerCancel(&timers[0]));
  EXPECT_FALSE(timer_list.TimerCancel(&timers[1]));
  EXPECT_FALSE(timer_list.TimerCancel(&timers[2]));

  // Timers 4 & 5 should be cancellable.
  EXPECT_TRUE(timer_list.TimerCancel(&timers[3]));
  EXPECT_TRUE(timer_list.TimerCancel(&timers[4]));
}

// Tests extension of timers.
TEST(SlackedTimerListTest, TimerExtend) {
  Timer timers[5];
  StrictMock<MockClosure> closures[5];

  const auto kStart =
      grpc_core::Timestamp::FromMillisecondsAfterProcessEpoch(0);

  StrictMock<MockHost> host;
  SlackedTimerList::Options options;
  options.num_shards = 5;
  options.resolution = grpc_core::Duration::Minutes(1);
  SlackedTimerList timer_list(&host, options);

  // Timer-1 with deadline 100ms in the future
  timer_list.TimerInit(&timers[0],
                       kStart + grpc_core::Duration::Milliseconds(100),
                       &closures[0]);

  // Timer-2 with deadline 1 min + 10ms in the future
  timer_list.TimerInit(&timers[1],
                       kStart + grpc_core::Duration::Minutes(1) +
                           grpc_core::Duration::Milliseconds(10),
                       &closures[1]);

  // Timer-3 with deadline 400ms in the future
  timer_list.TimerInit(&timers[2],
                       kStart + grpc_core::Duration::Milliseconds(400),
                       &closures[2]);

  // Timer-4 with deadline 1 min + 30s in the future
  timer_list.TimerInit(&timers[3],
                       kStart + grpc_core::Duration::Minutes(1) +
                           grpc_core::Duration::Seconds(30),
                       &closures[3]);

  // Timer-5 with deadline 2 mins + 10ms in the future
  timer_list.TimerInit(&timers[4],
                       kStart + grpc_core::Duration::Minutes(2) +
                           grpc_core::Duration::Milliseconds(10),
                       &closures[4]);

  // Extend timer-1 by 100ms
  EXPECT_TRUE(timer_list.TimerExtend(&timers[0],
                                     grpc_core::Duration::Milliseconds(100)));

  // Extend timer-2 by 1min
  EXPECT_TRUE(
      timer_list.TimerExtend(&timers[1], grpc_core::Duration::Minutes(1)));

  // Extend timer-3 by 3min
  EXPECT_TRUE(
      timer_list.TimerExtend(&timers[2], grpc_core::Duration::Minutes(3)));

  // Advance time by 1 min + 10ms
  EXPECT_CALL(host, Now())
      .WillOnce(Return(kStart + grpc_core::Duration::Minutes(1) +
                       grpc_core::Duration::Milliseconds(10)));
  // Timer 1 should have run.
  EXPECT_CALL(closures[0], Run());
  EXPECT_EQ(FinishCheck(timer_list.TimerCheck(nullptr)),
            CheckResult::kTimersFired);

  // Advance time to 2min
  EXPECT_CALL(host, Now())
      .WillOnce(Return(kStart + grpc_core::Duration::Minutes(2)));
  // Timers 2, 4 & 5 should have run.
  EXPECT_CALL(closures[1], Run());
  EXPECT_CALL(closures[3], Run());
  EXPECT_CALL(closures[4], Run());
  EXPECT_EQ(FinishCheck(timer_list.TimerCheck(nullptr)),
            CheckResult::kTimersFired);
  Mock::VerifyAndClearExpectations(&closures[1]);
  Mock::VerifyAndClearExpectations(&closures[3]);
  Mock::VerifyAndClearExpectations(&closures[4]);
  // Timer 3 should be cancellable.
  EXPECT_TRUE(timer_list.TimerCancel(&timers[2]));

  // Advance time to 3min
  EXPECT_CALL(host, Now())
      .WillOnce(Return(kStart + grpc_core::Duration::Minutes(3)));
  // No timers should be available to run.
  EXPECT_EQ(FinishCheck(timer_list.TimerCheck(nullptr)),
            CheckResult::kCheckedAndEmpty);
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
