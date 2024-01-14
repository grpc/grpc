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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

MATCHER(IsPending, "") {
  if (arg.ready()) {
    *result_listener << "is ready";
    return false;
  }
  return true;
}

MATCHER(IsReady, "") {
  if (arg.pending()) {
    *result_listener << "is pending";
    return false;
  }
  return true;
}

MATCHER_P(IsReady, value, "") {
  if (arg.pending()) {
    *result_listener << "is pending";
    return false;
  }
  if (arg.value() != value) {
    *result_listener << "is " << ::testing::PrintToString(arg.value());
    return false;
  }
  return true;
}

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

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  gpr_log_verbosity_init();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
