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

#include "src/core/lib/promise/observable.h"

#include <functional>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "test/core/promise/test_wakeup_schedulers.h"

using testing::MockFunction;
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
        return wait_set_.AddPending(Activity::current()->MakeOwningWaker());
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

TEST(ObservableTest, CanPushAndGet) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  Observable<int> observable;
  auto observer = observable.MakeObserver();
  auto activity = MakeActivity(
      [&observer]() {
        return Seq(observer.Get(), [](absl::optional<int> i) {
          return i == 42 ? absl::OkStatus() : absl::UnknownError("expected 42");
        });
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  observable.Push(42);
}

TEST(ObservableTest, CanNext) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  Observable<int> observable;
  auto observer = observable.MakeObserver();
  auto activity = MakeActivity(
      [&observer]() {
        return Seq(
            observer.Get(),
            [&observer](absl::optional<int> i) {
              EXPECT_EQ(i, 42);
              return observer.Next();
            },
            [](absl::optional<int> i) {
              return i == 1 ? absl::OkStatus()
                            : absl::UnknownError("expected 1");
            });
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  observable.Push(42);
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  observable.Push(1);
}

TEST(ObservableTest, CanWatch) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  Observable<int> observable;
  Barrier barrier;
  auto activity = MakeActivity(
      [&observable, &barrier]() {
        return observable.Watch(
            [&barrier](int x,
                       WatchCommitter* committer) -> Promise<absl::Status> {
              if (x == 3) {
                committer->Commit();
                return Seq(barrier.Wait(), Immediate(absl::OkStatus()));
              } else {
                return Never<absl::Status>();
              }
            });
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
  observable.Push(1);
  observable.Push(2);
  observable.Push(3);
  observable.Push(4);
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  barrier.Clear();
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
