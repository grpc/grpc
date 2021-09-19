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

#include "src/core/lib/promise/pipe.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/memory/memory.h"

#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"

using testing::MockFunction;
using testing::StrictMock;

namespace grpc_core {

TEST(PipeTest, CanSendAndReceive) {
  Pipe<int> pipe;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [&pipe] {
        return Seq(
            // Concurrently: send 42 into the pipe, and receive from the pipe.
            Join(pipe.sender.Push(42), pipe.receiver.Next()),
            // Once complete, verify successful sending and the received value
            // is 42.
            [](std::tuple<bool, absl::optional<int>> result) {
              EXPECT_EQ(result, std::make_tuple(true, absl::optional<int>(42)));
              return absl::OkStatus();
            });
      },
      NoCallbackScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

TEST(PipeTest, CanReceiveAndSend) {
  Pipe<int> pipe;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [&pipe] {
        return Seq(
            // Concurrently: receive from the pipe, and send 42 into the pipe.
            Join(pipe.receiver.Next(), pipe.sender.Push(42)),
            // Once complete, verify the received value is 42 and successful
            // sending.
            [](std::tuple<absl::optional<int>, bool> result) {
              EXPECT_EQ(result, std::make_tuple(absl::optional<int>(42), true));
              return absl::OkStatus();
            });
      },
      NoCallbackScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

TEST(PipeTest, CanSeeClosedOnSend) {
  Pipe<int> pipe;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  auto sender = std::move(pipe.sender);
  auto receiver =
      absl::make_unique<PipeReceiver<int>>(std::move(pipe.receiver));
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  // Push 42 onto the pipe - this will the pipe's one-deep send buffer.
  EXPECT_TRUE(NowOrNever(sender.Push(42)).has_value());
  MakeActivity(
      [&sender, &receiver] {
        return Seq(
            // Concurrently:
            // - push 43 into the sender, which will stall because the buffer is
            //   full
            // - and close the receiver, which will fail the pending send.
            Join(sender.Push(43),
                 [&receiver] {
                   receiver.reset();
                   return absl::OkStatus();
                 }),
            // Verify both that the send failed and that we executed the close.
            [](std::tuple<bool, absl::Status> result) {
              EXPECT_EQ(result, std::make_tuple(false, absl::OkStatus()));
              return absl::OkStatus();
            });
      },
      NoCallbackScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

TEST(PipeTest, CanSeeClosedOnReceive) {
  Pipe<int> pipe;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  auto sender = absl::make_unique<PipeSender<int>>(std::move(pipe.sender));
  auto receiver = std::move(pipe.receiver);
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [&sender, &receiver] {
        return Seq(
            // Concurrently:
            // - wait for a received value (will stall forever since we push
            //   nothing into the queue)
            // - close the sender, which will signal the receiver to return an
            //   end-of-stream.
            Join(receiver.Next(),
                 [&sender] {
                   sender.reset();
                   return absl::OkStatus();
                 }),
            // Verify we received end-of-stream and closed the sender.
            [](std::tuple<absl::optional<int>, absl::Status> result) {
              EXPECT_EQ(result, std::make_tuple(absl::optional<int>(),
                                                absl::OkStatus()));
              return absl::OkStatus();
            });
      },
      NoCallbackScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

TEST(PipeTest, CanFilter) {
  Pipe<int> pipe;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [&pipe] {
        // Setup some filters here, carefully getting ordering correct by doing
        // so outside of the Join() since C++ does not define execution order
        // between arguments.
        // TODO(ctiller): A future change to Pipe will specify an ordering
        // between filters added to sender and receiver, at which point these
        // should move back.
        auto doubler = pipe.receiver.Filter(
            [](int p) { return absl::StatusOr<int>(p * 2); });
        auto adder = pipe.sender.Filter(
            [](int p) { return absl::StatusOr<int>(p + 1); });
        return Seq(
            // Concurrently:
            // - push 42 into the pipe
            // - wait for a value to be received, and filter it by doubling it
            // - wait for a value to be received, and filter it by adding one to
            //   it
            // - wait for a value to be received and close the pipe.
            Join(pipe.sender.Push(42), std::move(doubler), std::move(adder),
                 Seq(pipe.receiver.Next(),
                     [&pipe](absl::optional<int> i) {
                       auto x = std::move(pipe.receiver);
                       return i;
                     })),
            // Verify all of the above happened correctly.
            [](std::tuple<bool, absl::Status, absl::Status, absl::optional<int>>
                   result) {
              EXPECT_EQ(result, std::make_tuple(true, absl::OkStatus(),
                                                absl::OkStatus(),
                                                absl::optional<int>(85)));
              return absl::OkStatus();
            });
      },
      NoCallbackScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
