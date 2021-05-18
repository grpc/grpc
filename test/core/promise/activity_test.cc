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
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"

using testing::_;
using testing::Mock;
using testing::MockFunction;
using testing::SaveArg;
using testing::StrictMock;

namespace grpc_core {

class MockCallbackScheduler : public CallbackScheduler {
 public:
  MOCK_METHOD(void, Schedule, (std::function<void()>));
};

// A simple Barrier type: stalls progress until it is 'cleared'.
// This variant supports multiple waiters.
class Barrier {
 public:
  struct Result {};

  Promise<Result> Wait() {
    return [this]() -> Poll<Result> {
      absl::MutexLock lock(wait_set_.mu());
      if (cleared_) {
        return ready(Result{});
      } else {
        return wait_set_.pending();
      }
    };
  }

  void Clear() {
    wait_set_.mu()->Lock();
    cleared_ = true;
    wait_set_.WakeAllAndUnlock();
  }

 private:
  WaitSet wait_set_;
  bool cleared_ GUARDED_BY(wait_set_.mu()) = false;
};

// A simple Barrier type: stalls progress until it is 'cleared'.
// This variant supports only a single waiter.
class SingleBarrier {
 public:
  struct Result {};

  Promise<Result> Wait() {
    return [this]() -> Poll<Result> {
      absl::MutexLock lock(waiter_.mu());
      if (cleared_) {
        return ready(Result{});
      } else {
        return waiter_.pending();
      }
    };
  }

  void Clear() {
    waiter_.mu()->Lock();
    cleared_ = true;
    waiter_.WakeAndUnlock();
  }

 private:
  SingleWaiter waiter_;
  bool cleared_ GUARDED_BY(waiter_.mu()) = false;
};

TEST(ActivityTest, ImmediatelyCompleteWithSuccess) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  ActivityFromPromiseFactory(
      [] { return [] { return ready(absl::OkStatus()); }; },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
}

TEST(ActivityTest, ImmediatelyCompleteWithFailure) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::CancelledError()));
  ActivityFromPromiseFactory(
      [] { return [] { return ready(absl::CancelledError()); }; },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
}

TEST(ActivityTest, DropImmediately) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::CancelledError()));
  ActivityFromPromiseFactory(
      [] { return []() -> Poll<absl::Status> { return kPending; }; },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
}

TEST(ActivityTest, Cancel) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  auto activity = ActivityFromPromiseFactory(
      [] { return []() -> Poll<absl::Status> { return kPending; }; },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
  EXPECT_CALL(on_done, Call(absl::CancelledError()));
  activity->Cancel();
  Mock::VerifyAndClearExpectations(&on_done);
  activity.reset();
}

template <typename B>
class BarrierTest : public testing::Test {
 public:
  using Type = B;
};

using BarrierTestTypes = testing::Types<Barrier, SingleBarrier>;
TYPED_TEST_SUITE(BarrierTest, BarrierTestTypes);

TYPED_TEST(BarrierTest, Barrier) {
  typename TestFixture::Type b;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  auto activity = ActivityFromPromiseFactory(
      [&b] {
        return Seq(b.Wait(), [](typename TestFixture::Type::Result) {
          return ready(absl::OkStatus());
        });
      },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
  // Clearing the barrier should let the activity proceed to return a result.
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  b.Clear();
}

TYPED_TEST(BarrierTest, BarrierPing) {
  typename TestFixture::Type b1;
  typename TestFixture::Type b2;
  StrictMock<MockFunction<void(absl::Status)>> on_done1;
  StrictMock<MockFunction<void(absl::Status)>> on_done2;
  MockCallbackScheduler scheduler;
  auto activity1 = ActivityFromPromiseFactory(
      [&b1, &b2] {
        return Seq(b1.Wait(), [&b2](typename TestFixture::Type::Result) {
          // Clear the barrier whilst executing an activity
          b2.Clear();
          return ready(absl::OkStatus());
        });
      },
      [&on_done1](absl::Status status) { on_done1.Call(std::move(status)); },
      &scheduler);
  auto activity2 = ActivityFromPromiseFactory(
      [&b2] {
        return Seq(b2.Wait(), [](typename TestFixture::Type::Result) {
          return ready(absl::OkStatus());
        });
      },
      [&on_done2](absl::Status status) { on_done2.Call(std::move(status)); },
      &scheduler);
  EXPECT_CALL(on_done1, Call(absl::OkStatus()));
  // Since barrier triggers inside activity1 promise, activity2 wakeup will be
  // scheduled from a callback.
  std::function<void()> cb;
  EXPECT_CALL(scheduler, Schedule(_)).WillOnce(SaveArg<0>(&cb));
  b1.Clear();
  Mock::VerifyAndClearExpectations(&on_done1);
  EXPECT_CALL(on_done2, Call(absl::OkStatus()));
  cb();
}

TYPED_TEST(BarrierTest, WakeSelf) {
  typename TestFixture::Type b;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  ActivityFromPromiseFactory(
      [&b] {
        return Seq(Join(b.Wait(),
                        [&b] {
                          b.Clear();
                          return ready(1);
                        }),
                   [](std::tuple<typename TestFixture::Type::Result, int>) {
                     return ready(absl::OkStatus());
                   });
      },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
}

TYPED_TEST(BarrierTest, WakeAfterDestruction) {
  typename TestFixture::Type b;
  {
    StrictMock<MockFunction<void(absl::Status)>> on_done;
    EXPECT_CALL(on_done, Call(absl::CancelledError()));
    ActivityFromPromiseFactory(
        [&b] {
          return Seq(b.Wait(), [](typename TestFixture::Type::Result) {
            return ready(absl::OkStatus());
          });
        },
        [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
        nullptr);
  }
  b.Clear();
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
