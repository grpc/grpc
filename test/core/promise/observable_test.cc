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
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "src/core/lib/promise/seq.h"

using testing::MockFunction;
using testing::StrictMock;

namespace grpc_core {

TEST(ObservableTest, CanPushAndGet) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  Observable<int> observable;
  auto observer = observable.MakeObserver();
  auto activity = ActivityFromPromiseFactory(
      [&observer]() {
        return Seq(observer.Get(), [](absl::optional<int> i) {
          return ready(i == 42 ? absl::OkStatus()
                               : absl::UnknownError("expected 42"));
        });
      },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  observable.Push(42);
}

TEST(ObservableTest, CanNext) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  Observable<int> observable;
  auto observer = observable.MakeObserver();
  auto activity = ActivityFromPromiseFactory(
      [&observer]() {
        return Seq(
            observer.Get(),
            [&observer](absl::optional<int> i) {
              EXPECT_EQ(i, 42);
              return observer.Next();
            },
            [](absl::optional<int> i) {
              return ready(i == 1 ? absl::OkStatus()
                                  : absl::UnknownError("expected 1"));
            });
      },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
  observable.Push(42);
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  observable.Push(1);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
