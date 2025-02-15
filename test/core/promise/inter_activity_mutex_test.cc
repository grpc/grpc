// Copyright 2023 gRPC authors.
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

#include "src/core/lib/promise/inter_activity_mutex.h"

#include <grpc/grpc.h>

#include "gtest/gtest.h"
#include "test/core/promise/poll_matcher.h"

using ::testing::Mock;
using ::testing::StrictMock;

namespace grpc_core {
namespace {

int unused = (grpc_tracer_init(), 0);

// A mock activity that can be activated and deactivated.
class MockActivity : public Activity, public Wakeable {
 public:
  MOCK_METHOD(void, WakeupRequested, ());

  void ForceImmediateRepoll(WakeupMask /*mask*/) override { WakeupRequested(); }
  void Orphan() override {}
  Waker MakeOwningWaker() override { return Waker(this, 0); }
  Waker MakeNonOwningWaker() override { return Waker(this, 0); }
  void Wakeup(WakeupMask /*mask*/) override { WakeupRequested(); }
  void WakeupAsync(WakeupMask /*mask*/) override { WakeupRequested(); }
  void Drop(WakeupMask /*mask*/) override {}
  std::string DebugTag() const override { return "MockActivity"; }
  std::string ActivityDebugTag(WakeupMask /*mask*/) const override {
    return DebugTag();
  }

  void Activate() {
    if (scoped_activity_ == nullptr) {
      scoped_activity_ = std::make_unique<ScopedActivity>(this);
    }
  }

  void Deactivate() { scoped_activity_.reset(); }

 private:
  std::unique_ptr<ScopedActivity> scoped_activity_;
};

#define EXPECT_WAKEUP(activity, statement)                                 \
  EXPECT_CALL((activity), WakeupRequested()).Times(::testing::AtLeast(1)); \
  statement;                                                               \
  Mock::VerifyAndClearExpectations(&(activity));

template <typename Lock>
void Drop(Lock lock) {}

TEST(InterActivityMutexTest, Basic) {
  InterActivityMutex<int> mutex(42);
  auto acq = mutex.Acquire();
  auto lock = acq();
  EXPECT_THAT(lock, IsReady());
  EXPECT_EQ(*lock.value(), 42);
}

TEST(InterActivityMutexTest, TwoAcquires) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  InterActivityMutex<int> mutex(42);
  auto acq1 = mutex.Acquire();
  auto acq2 = mutex.Acquire();
  auto lock1 = acq1();
  auto lock2 = acq2();
  EXPECT_THAT(lock1, IsReady());
  EXPECT_EQ(*lock1.value(), 42);
  *lock1.value() = 43;
  EXPECT_THAT(lock2, IsPending());
  lock2 = acq2();
  EXPECT_THAT(lock2, IsPending());
  EXPECT_WAKEUP(activity, Drop(std::move(lock1.value())));
  lock2 = acq2();
  EXPECT_THAT(lock2, IsReady());
  EXPECT_EQ(*lock2.value(), 43);
}

TEST(InterActivityMutexTest, ThreeAcquires) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  InterActivityMutex<int> mutex(42);
  auto acq1 = mutex.Acquire();
  auto acq2 = mutex.Acquire();
  auto acq3 = mutex.Acquire();
  auto lock1 = acq1();
  auto lock2 = acq2();
  auto lock3 = acq3();
  EXPECT_THAT(lock1, IsReady());
  EXPECT_THAT(lock2, IsPending());
  EXPECT_THAT(lock3, IsPending());
  EXPECT_EQ(*lock1.value(), 42);
  EXPECT_WAKEUP(activity, Drop(std::move(lock1.value())));
  lock3 = acq3();
  lock2 = acq2();
  EXPECT_THAT(lock2, IsReady());
  EXPECT_THAT(lock3, IsPending());
  EXPECT_WAKEUP(activity, Drop(std::move(lock2.value())));
  lock3 = acq3();
  EXPECT_THAT(lock3, IsReady());
  EXPECT_EQ(*lock3.value(), 42);
}

TEST(InterActivityMutexTest, ThreeAcquiresWithCancelledAcquisition) {
  StrictMock<MockActivity> activity;
  activity.Activate();
  InterActivityMutex<int> mutex(42);
  auto acq1 = mutex.Acquire();
  auto acq2 = mutex.Acquire();
  auto acq3 = mutex.Acquire();
  auto lock1 = acq1();
  auto lock2 = acq2();
  auto lock3 = acq3();
  EXPECT_THAT(lock1, IsReady());
  EXPECT_THAT(lock2, IsPending());
  EXPECT_THAT(lock3, IsPending());
  EXPECT_EQ(*lock1.value(), 42);
  EXPECT_WAKEUP(activity, Drop(std::move(lock1)));
  EXPECT_WAKEUP(activity, Drop(std::move(acq2)));
  lock3 = acq3();
  EXPECT_THAT(lock3, IsReady());
  EXPECT_EQ(*lock3.value(), 42);
}

}  // namespace
}  // namespace grpc_core
