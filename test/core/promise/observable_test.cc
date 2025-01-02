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

#include "src/core/lib/promise/observable.h"

#include <grpc/support/log.h>

#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

#include "absl/strings/str_join.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/util/notification.h"
#include "test/core/promise/poll_matcher.h"

using testing::Mock;
using testing::StrictMock;

namespace grpc_core {
namespace {

class MockActivity : public Activity, public Wakeable {
 public:
  MOCK_METHOD(void, WakeupRequested, ());

  void ForceImmediateRepoll(WakeupMask) override { WakeupRequested(); }
  void Orphan() override {}
  Waker MakeOwningWaker() override { return Waker(this, 0); }
  Waker MakeNonOwningWaker() override { return Waker(this, 0); }
  void Wakeup(WakeupMask) override { WakeupRequested(); }
  void WakeupAsync(WakeupMask) override { WakeupRequested(); }
  void Drop(WakeupMask) override {}
  std::string DebugTag() const override { return "MockActivity"; }
  std::string ActivityDebugTag(WakeupMask) const override { return DebugTag(); }

  void Activate() {
    if (scoped_activity_ != nullptr) return;
    scoped_activity_ = std::make_unique<ScopedActivity>(this);
  }

  void Deactivate() { scoped_activity_.reset(); }

 private:
  std::unique_ptr<ScopedActivity> scoped_activity_;
};

TEST(ObservableTest, ImmediateNext) {
  Observable<int> observable(1);
  auto next = observable.Next(0);
  EXPECT_THAT(next(), IsReady(1));
}

TEST(ObservableTest, SetBecomesImmediateNext1) {
  Observable<int> observable(0);
  auto next = observable.Next(0);
  observable.Set(1);
  EXPECT_THAT(next(), IsReady(1));
}

TEST(ObservableTest, SetBecomesImmediateNext2) {
  Observable<int> observable(0);
  observable.Set(1);
  auto next = observable.Next(0);
  EXPECT_THAT(next(), IsReady(1));
}

TEST(ObservableTest, SameValueGetsPending) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  Observable<int> observable(1);
  auto next = observable.Next(1);
  EXPECT_THAT(next(), IsPending());
  EXPECT_THAT(next(), IsPending());
  EXPECT_THAT(next(), IsPending());
  EXPECT_THAT(next(), IsPending());
}

TEST(ObservableTest, ChangeValueWakesUp) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  Observable<int> observable(1);
  auto next = observable.Next(1);
  EXPECT_THAT(next(), IsPending());
  EXPECT_CALL(activity, WakeupRequested());
  observable.Set(2);
  Mock::VerifyAndClearExpectations(&activity);
  EXPECT_THAT(next(), IsReady(2));
}

TEST(ObservableTest, NextWhen) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  Observable<int> observable(1);
  auto next = observable.NextWhen([](int i) { return i == 3; });
  EXPECT_THAT(next(), IsPending());
  EXPECT_CALL(activity, WakeupRequested());
  observable.Set(2);
  EXPECT_THAT(next(), IsPending());
  EXPECT_CALL(activity, WakeupRequested());
  observable.Set(3);
  Mock::VerifyAndClearExpectations(&activity);
  EXPECT_THAT(next(), IsReady(3));
}

TEST(ObservableTest, MultipleActivitiesWakeUp) {
  StrictMock<MockActivity> activity1;
  StrictMock<MockActivity> activity2;
  Observable<int> observable(1);
  auto next1 = observable.Next(1);
  auto next2 = observable.Next(1);
  {
    activity1.Activate();
    EXPECT_THAT(next1(), IsPending());
  }
  {
    activity2.Activate();
    EXPECT_THAT(next2(), IsPending());
  }
  EXPECT_CALL(activity1, WakeupRequested());
  EXPECT_CALL(activity2, WakeupRequested());
  observable.Set(2);
  Mock::VerifyAndClearExpectations(&activity1);
  Mock::VerifyAndClearExpectations(&activity2);
  EXPECT_THAT(next1(), IsReady(2));
  EXPECT_THAT(next2(), IsReady(2));
}

TEST(ObservableTest, NoDeadlockOnDestruction) {
  StrictMock<MockActivity> activity;
  Observable<int> observable(1);
  activity.Activate();
  {
    auto next = observable.Next(1);
    EXPECT_THAT(next(), IsPending());
  }
}

class ThreadWakeupScheduler {
 public:
  template <typename ActivityType>
  class BoundScheduler {
   public:
    explicit BoundScheduler(ThreadWakeupScheduler) {}
    void ScheduleWakeup() {
      std::thread t(
          [this] { static_cast<ActivityType*>(this)->RunScheduledWakeup(); });
      t.detach();
    }
  };
};

TEST(ObservableTest, Stress) {
  static constexpr uint64_t kEnd = std::numeric_limits<uint64_t>::max();
  std::vector<uint64_t> values1;
  std::vector<uint64_t> values2;
  uint64_t current1 = 0;
  uint64_t current2 = 0;
  Notification done1;
  Notification done2;
  Observable<uint64_t> observable(0);
  auto activity1 = MakeActivity(
      Loop([&observable, &current1, &values1] {
        return Map(
            observable.Next(current1),
            [&values1, &current1](uint64_t value) -> LoopCtl<absl::Status> {
              values1.push_back(value);
              current1 = value;
              if (value == kEnd) return absl::OkStatus();
              return Continue{};
            });
      }),
      ThreadWakeupScheduler(), [&done1](absl::Status status) {
        EXPECT_TRUE(status.ok()) << status.ToString();
        done1.Notify();
      });
  auto activity2 = MakeActivity(
      Loop([&observable, &current2, &values2] {
        return Map(
            observable.Next(current2),
            [&values2, &current2](uint64_t value) -> LoopCtl<absl::Status> {
              values2.push_back(value);
              current2 = value;
              if (value == kEnd) return absl::OkStatus();
              return Continue{};
            });
      }),
      ThreadWakeupScheduler(), [&done2](absl::Status status) {
        EXPECT_TRUE(status.ok()) << status.ToString();
        done2.Notify();
      });
  for (uint64_t i = 0; i < 1000000; i++) {
    observable.Set(i);
  }
  observable.Set(kEnd);
  done1.WaitForNotification();
  done2.WaitForNotification();
  ASSERT_GE(values1.size(), 1);
  ASSERT_GE(values2.size(), 1);
  EXPECT_EQ(values1.back(), kEnd);
  EXPECT_EQ(values2.back(), kEnd);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  gpr_log_verbosity_init();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
