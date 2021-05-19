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

#include "absl/memory/memory.h"
#include "src/core/lib/promise/pipe.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/seq.h"

using testing::MockFunction;
using testing::StrictMock;

namespace grpc_core {

TEST(PipeTest, CanSendAndReceive) {
  Pipe<int> pipe;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  ActivityFromPromiseFactory(
      [&pipe] {
        return Seq(Join(pipe.sender.Push(42), pipe.receiver.Next()),
                   [](std::tuple<bool, absl::optional<int>> result) {
                     EXPECT_EQ(result,
                               std::make_tuple(true, absl::optional<int>(42)));
                     return ready(absl::OkStatus());
                   });
      },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
}

TEST(PipeTest, CanReceiveAndSend) {
  Pipe<int> pipe;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  ActivityFromPromiseFactory(
      [&pipe] {
        return Seq(Join(pipe.receiver.Next(), pipe.sender.Push(42)),
                   [](std::tuple<absl::optional<int>, bool> result) {
                     EXPECT_EQ(result,
                               std::make_tuple(absl::optional<int>(42), true));
                     return ready(absl::OkStatus());
                   });
      },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
}

TEST(PipeTest, CanSeeClosedOnSend) {
  Pipe<int> pipe;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  auto sender = std::move(pipe.sender);
  auto receiver = absl::make_unique<PipeReceiver<int>>(
      std::move(pipe.receiver));
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  ActivityFromPromiseFactory(
      [&sender, &receiver] {
        return Seq(Join(sender.Push(42), sender.Push(43),
                        [&receiver] {
                          receiver.reset();
                          return ready(absl::OkStatus());
                        }),
                   [](std::tuple<bool, bool, absl::Status> result) {
                     EXPECT_EQ(result,
                               std::make_tuple(true, false, absl::OkStatus()));
                     return ready(absl::OkStatus());
                   });
      },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
}

TEST(PipeTest, CanSeeClosedOnReceive) {
  Pipe<int> pipe;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  auto sender = absl::make_unique<PipeSender<int>>(
      std::move(pipe.sender));
  auto receiver = std::move(pipe.receiver);
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  ActivityFromPromiseFactory(
      [&sender, &receiver] {
        return Seq(Join(receiver.Next(),
                        [&sender] {
                          sender.reset();
                          return ready(absl::OkStatus());
                        }),
                   [](std::tuple<absl::optional<int>, absl::Status> result) {
                     EXPECT_EQ(result, std::make_tuple(absl::optional<int>(),
                                                       absl::OkStatus()));
                     return ready(absl::OkStatus());
                   });
      },
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      nullptr);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
