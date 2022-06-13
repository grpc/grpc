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

#include <memory>
#include <tuple>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/detail/basic_seq.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/promise/test_wakeup_schedulers.h"

using testing::MockFunction;
using testing::StrictMock;

namespace grpc_core {

static auto* g_memory_allocator = new MemoryAllocator(
    ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));

TEST(PipeTest, CanSendAndReceive) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        Pipe<int> pipe;
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
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, g_memory_allocator));
}

TEST(PipeTest, CanReceiveAndSend) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        Pipe<int> pipe;
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
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, g_memory_allocator));
}

TEST(PipeTest, CanSeeClosedOnSend) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        Pipe<int> pipe;
        auto sender = std::move(pipe.sender);
        // Push 42 onto the pipe - this will the pipe's one-deep send buffer.
        EXPECT_TRUE(NowOrNever(sender.Push(42)).has_value());
        auto receiver = std::make_shared<std::unique_ptr<PipeReceiver<int>>>(
            absl::make_unique<PipeReceiver<int>>(std::move(pipe.receiver)));
        return Seq(
            // Concurrently:
            // - push 43 into the sender, which will stall because the buffer is
            //   full
            // - and close the receiver, which will fail the pending send.
            Join(sender.Push(43),
                 [receiver] {
                   receiver->reset();
                   return absl::OkStatus();
                 }),
            // Verify both that the send failed and that we executed the close.
            [](std::tuple<bool, absl::Status> result) {
              EXPECT_EQ(result, std::make_tuple(false, absl::OkStatus()));
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, g_memory_allocator));
}

TEST(PipeTest, CanSeeClosedOnReceive) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        Pipe<int> pipe;
        auto sender = std::make_shared<std::unique_ptr<PipeSender<int>>>(
            absl::make_unique<PipeSender<int>>(std::move(pipe.sender)));
        auto receiver = std::move(pipe.receiver);
        return Seq(
            // Concurrently:
            // - wait for a received value (will stall forever since we push
            //   nothing into the queue)
            // - close the sender, which will signal the receiver to return an
            //   end-of-stream.
            Join(receiver.Next(),
                 [sender] {
                   sender->reset();
                   return absl::OkStatus();
                 }),
            // Verify we received end-of-stream and closed the sender.
            [](std::tuple<absl::optional<int>, absl::Status> result) {
              EXPECT_EQ(result, std::make_tuple(absl::optional<int>(),
                                                absl::OkStatus()));
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, g_memory_allocator));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
