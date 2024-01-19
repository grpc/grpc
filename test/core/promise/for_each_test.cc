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

#include "src/core/lib/promise/for_each.h"

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/inter_activity_pipe.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/promise/test_wakeup_schedulers.h"

using testing::Mock;
using testing::MockFunction;
using testing::StrictMock;

namespace grpc_core {

class ForEachTest : public ::testing::Test {
 protected:
  MemoryAllocator memory_allocator_ = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
};

TEST_F(ForEachTest, SendThriceWithPipe) {
  int num_received = 0;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [&num_received] {
        Pipe<int> pipe;
        auto sender = std::make_shared<std::unique_ptr<PipeSender<int>>>(
            std::make_unique<PipeSender<int>>(std::move(pipe.sender)));
        return Map(
            Join(
                // Push 3 things into a pipe -- 1, 2, then 3 -- then close.
                Seq((*sender)->Push(1), [sender] { return (*sender)->Push(2); },
                    [sender] { return (*sender)->Push(3); },
                    [sender] {
                      sender->reset();
                      return absl::OkStatus();
                    }),
                // Use a ForEach loop to read them out and verify all values are
                // seen.
                ForEach(std::move(pipe.receiver),
                        [&num_received](int i) {
                          num_received++;
                          EXPECT_EQ(num_received, i);
                          return absl::OkStatus();
                        })),
            JustElem<1>());
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
  Mock::VerifyAndClearExpectations(&on_done);
  EXPECT_EQ(num_received, 3);
}

TEST_F(ForEachTest, SendThriceWithInterActivityPipe) {
  int num_received = 0;
  StrictMock<MockFunction<void(absl::Status)>> on_done_sender;
  StrictMock<MockFunction<void(absl::Status)>> on_done_receiver;
  EXPECT_CALL(on_done_sender, Call(absl::OkStatus()));
  EXPECT_CALL(on_done_receiver, Call(absl::OkStatus()));
  InterActivityPipe<int, 1> pipe;
  auto send_activity = MakeActivity(
      Seq(
          // Push 3 things into a pipe -- 1, 2, then 3 -- then close.
          pipe.sender.Push(1), [&pipe] { return pipe.sender.Push(2); },
          [&pipe] { return pipe.sender.Push(3); },
          [&pipe] {
            auto x = std::move(pipe.sender);
            return absl::OkStatus();
          }),
      InlineWakeupScheduler{}, [&on_done_sender](absl::Status status) {
        on_done_sender.Call(std::move(status));
      });
  MakeActivity(
      [&num_received, &pipe] {
        // Use a ForEach loop to read them out and verify
        // all values are seen.
        return ForEach(std::move(pipe.receiver), [&num_received](int i) {
          num_received++;
          EXPECT_EQ(num_received, i);
          return absl::OkStatus();
        });
      },
      NoWakeupScheduler(),
      [&on_done_receiver](absl::Status status) {
        on_done_receiver.Call(std::move(status));
      });
  Mock::VerifyAndClearExpectations(&on_done_sender);
  Mock::VerifyAndClearExpectations(&on_done_receiver);
  EXPECT_EQ(num_received, 3);
}

// Pollable type that stays movable until it's polled, then causes the test to
// fail if it's moved again.
// Promises have the property that they can be moved until polled, and this
// helps us check that the internals of ForEach respect this rule.
class MoveableUntilPolled {
 public:
  MoveableUntilPolled() = default;
  MoveableUntilPolled(const MoveableUntilPolled&) = delete;
  MoveableUntilPolled& operator=(const MoveableUntilPolled&) = delete;
  MoveableUntilPolled(MoveableUntilPolled&& other) noexcept : polls_(0) {
    EXPECT_EQ(other.polls_, 0);
  }
  MoveableUntilPolled& operator=(MoveableUntilPolled&& other) noexcept {
    EXPECT_EQ(other.polls_, 0);
    polls_ = 0;
    return *this;
  }

  Poll<absl::Status> operator()() {
    GetContext<Activity>()->ForceImmediateRepoll();
    ++polls_;
    if (polls_ == 10) return absl::OkStatus();
    return Pending();
  }

 private:
  int polls_ = 0;
};

TEST_F(ForEachTest, NoMoveAfterPoll) {
  int num_received = 0;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [&num_received] {
        Pipe<int> pipe;
        auto sender = std::make_shared<std::unique_ptr<PipeSender<int>>>(
            std::make_unique<PipeSender<int>>(std::move(pipe.sender)));
        return Map(
            Join(
                // Push one things into a pipe, then close.
                Seq((*sender)->Push(1),
                    [sender] {
                      sender->reset();
                      return absl::OkStatus();
                    }),
                // Use a ForEach loop to read them out and verify all
                // values are seen.
                // Inject a MoveableUntilPolled into the loop to ensure that
                // ForEach doesn't internally move a promise post-polling.
                ForEach(std::move(pipe.receiver),
                        [&num_received](int i) {
                          num_received++;
                          EXPECT_EQ(num_received, i);
                          return MoveableUntilPolled();
                        })),
            JustElem<1>());
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
  Mock::VerifyAndClearExpectations(&on_done);
  EXPECT_EQ(num_received, 1);
}

TEST_F(ForEachTest, NextResultHeldThroughCallback) {
  int num_received = 0;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [&num_received] {
        Pipe<int> pipe;
        auto sender = std::make_shared<std::unique_ptr<PipeSender<int>>>(
            std::make_unique<PipeSender<int>>(std::move(pipe.sender)));
        return Map(
            Join(
                // Push one things into a pipe, then close.
                Seq((*sender)->Push(1),
                    [sender] {
                      sender->reset();
                      return absl::OkStatus();
                    }),
                // Use a ForEach loop to read them out and verify all
                // values are seen.
                ForEach(std::move(pipe.receiver),
                        [&num_received, sender](int i) {
                          // While we're processing a value NextResult
                          // should be held disallowing new items to be
                          // pushed.
                          // We also should not have reached the
                          // sender->reset() line above yet either, as
                          // the Push() should block until this code
                          // completes.
                          EXPECT_TRUE((*sender)->Push(2)().pending());
                          num_received++;
                          EXPECT_EQ(num_received, i);
                          return TrySeq(
                              // has the side effect of stalling for some
                              // iterations
                              MoveableUntilPolled(), [sender] {
                                // Perform the same test verifying the same
                                // properties for NextResult holding: all should
                                // still be true.
                                EXPECT_TRUE((*sender)->Push(2)().pending());
                                return absl::OkStatus();
                              });
                        })),
            JustElem<1>());
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
  Mock::VerifyAndClearExpectations(&on_done);
  EXPECT_EQ(num_received, 1);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
