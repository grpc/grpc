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

#include "src/core/lib/promise/activity.h"

#include <stdlib.h>

#include <functional>
#include <tuple>
#include <variant>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/wait_set.h"
#include "test/core/promise/test_wakeup_schedulers.h"

using testing::_;
using testing::Mock;
using testing::MockFunction;
using testing::SaveArg;
using testing::StrictMock;

namespace grpc_core {

// A simple Barrier type: stalls progress until it is 'cleared'.
class Barrier {
 public:
  struct Result {};

  Promise<Result> Wait() {
    return [this]() -> Poll<Result> {
      MutexLock lock(&mu_);
      if (cleared_) {
        return Result{};
      } else {
        return wait_set_.AddPending(GetContext<Activity>()->MakeOwningWaker());
      }
    };
  }

  void Clear() {
    mu_.Lock();
    cleared_ = true;
    auto wakeup = wait_set_.TakeWakeupSet();
    mu_.Unlock();
    wakeup.Wakeup();
  }

 private:
  Mutex mu_;
  WaitSet wait_set_ ABSL_GUARDED_BY(mu_);
  bool cleared_ ABSL_GUARDED_BY(mu_) = false;
};

// A simple Barrier type: stalls progress until it is 'cleared'.
// This variant supports only a single waiter.
class SingleBarrier {
 public:
  struct Result {};

  Promise<Result> Wait() {
    return [this]() -> Poll<Result> {
      MutexLock lock(&mu_);
      if (cleared_) {
        return Result{};
      } else {
        waker_ = GetContext<Activity>()->MakeOwningWaker();
        return Pending();
      }
    };
  }

  void Clear() {
    mu_.Lock();
    cleared_ = true;
    auto waker = std::move(waker_);
    mu_.Unlock();
    waker.Wakeup();
  }

 private:
  Mutex mu_;
  Waker waker_ ABSL_GUARDED_BY(mu_);
  bool cleared_ ABSL_GUARDED_BY(mu_) = false;
};

TEST(ActivityTest, ImmediatelyCompleteWithSuccess) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] { return [] { return absl::OkStatus(); }; }, NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

TEST(ActivityTest, ImmediatelyCompleteWithFailure) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::CancelledError()));
  MakeActivity(
      [] { return [] { return absl::CancelledError(); }; }, NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

TEST(ActivityTest, DropImmediately) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::CancelledError()));
  MakeActivity(
      [] { return []() -> Poll<absl::Status> { return Pending(); }; },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

template <typename B>
class BarrierTest : public ::testing::Test {
 public:
  using Type = B;
};

using BarrierTestTypes = ::testing::Types<Barrier, SingleBarrier>;
TYPED_TEST_SUITE(BarrierTest, BarrierTestTypes);

TYPED_TEST(BarrierTest, Barrier) {
  typename TestFixture::Type b;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  auto activity = MakeActivity(
      [&b] {
        return Seq(b.Wait(), [](typename TestFixture::Type::Result) {
          return absl::OkStatus();
        });
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  // Clearing the barrier should let the activity proceed to return a result.
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  b.Clear();
}

TYPED_TEST(BarrierTest, BarrierPing) {
  typename TestFixture::Type b1;
  typename TestFixture::Type b2;
  StrictMock<MockFunction<void(absl::Status)>> on_done1;
  StrictMock<MockFunction<void(absl::Status)>> on_done2;
  MockCallbackScheduler scheduler1;
  MockCallbackScheduler scheduler2;
  auto activity1 = MakeActivity(
      [&b1, &b2] {
        return Seq(b1.Wait(), [&b2](typename TestFixture::Type::Result) {
          // Clear the barrier whilst executing an activity
          b2.Clear();
          return absl::OkStatus();
        });
      },
      UseMockCallbackScheduler{&scheduler1},
      [&on_done1](absl::Status status) { on_done1.Call(std::move(status)); });
  auto activity2 = MakeActivity(
      [&b2] {
        return Seq(b2.Wait(), [](typename TestFixture::Type::Result) {
          return absl::OkStatus();
        });
      },
      UseMockCallbackScheduler{&scheduler2},
      [&on_done2](absl::Status status) { on_done2.Call(std::move(status)); });
  // Since barrier triggers inside activity1 promise, activity2 wakeup will be
  // scheduled from a callback.
  std::function<void()> cb1;
  std::function<void()> cb2;
  EXPECT_CALL(scheduler1, Schedule(_)).WillOnce(SaveArg<0>(&cb1));
  b1.Clear();
  Mock::VerifyAndClearExpectations(&scheduler1);
  EXPECT_CALL(on_done1, Call(absl::OkStatus()));
  EXPECT_CALL(scheduler2, Schedule(_)).WillOnce(SaveArg<0>(&cb2));
  cb1();
  Mock::VerifyAndClearExpectations(&on_done1);
  EXPECT_CALL(on_done2, Call(absl::OkStatus()));
  cb2();
}

TYPED_TEST(BarrierTest, WakeSelf) {
  typename TestFixture::Type b;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [&b] {
        return Seq(Join(b.Wait(),
                        [&b] {
                          b.Clear();
                          return 1;
                        }),
                   [](std::tuple<typename TestFixture::Type::Result, int>) {
                     return absl::OkStatus();
                   });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

TYPED_TEST(BarrierTest, WakeAfterDestruction) {
  typename TestFixture::Type b;
  {
    StrictMock<MockFunction<void(absl::Status)>> on_done;
    EXPECT_CALL(on_done, Call(absl::CancelledError()));
    MakeActivity(
        [&b] {
          return Seq(b.Wait(), [](typename TestFixture::Type::Result) {
            return absl::OkStatus();
          });
        },
        InlineWakeupScheduler(),
        [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  }
  b.Clear();
}

TEST(ActivityTest, ForceWakeup) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  int run_count = 0;
  auto activity = MakeActivity(
      [&run_count]() -> Poll<absl::Status> {
        ++run_count;
        switch (run_count) {
          case 1:
            return Pending{};
          case 2:
            return absl::OkStatus();
          default:
            abort();
        }
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  activity->ForceWakeup();
}

struct TestContext {
  bool* done;
};
template <>
struct ContextType<TestContext> {};

TEST(ActivityTest, WithContext) {
  bool done = false;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        *GetContext<TestContext>()->done = true;
        return Immediate(absl::OkStatus());
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      TestContext{&done});
  EXPECT_TRUE(done);
}

TEST(ActivityTest, CanCancelDuringExecution) {
  ActivityPtr activity;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  int run_count = 0;

  activity = MakeActivity(
      [&activity, &run_count]() -> Poll<absl::Status> {
        ++run_count;
        switch (run_count) {
          case 1:
            return Pending{};
          case 2:
            activity.reset();
            return Pending{};
          default:
            abort();
        }
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });

  EXPECT_CALL(on_done, Call(absl::CancelledError()));
  activity->ForceWakeup();
}

TEST(ActivityTest, CanCancelDuringSuccessfulExecution) {
  ActivityPtr activity;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  int run_count = 0;

  activity = MakeActivity(
      [&activity, &run_count]() -> Poll<absl::Status> {
        ++run_count;
        switch (run_count) {
          case 1:
            return Pending{};
          case 2:
            activity.reset();
            return absl::OkStatus();
          default:
            abort();
        }
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });

  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  activity->ForceWakeup();
}

TEST(WakerTest, CanWakeupEmptyWaker) {
  // Empty wakers should not do anything upon wakeup.
  Waker().Wakeup();
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
