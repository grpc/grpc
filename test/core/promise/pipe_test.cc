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

#include <initializer_list>
#include <memory>
#include <tuple>
#include <utility>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/promise/test_wakeup_schedulers.h"

using testing::MockFunction;
using testing::StrictMock;

namespace grpc_core {

class PipeTest : public ::testing::Test {
 protected:
  MemoryAllocator memory_allocator_ = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
};

TEST_F(PipeTest, CanSendAndReceive) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        auto* pipe = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        return Seq(
            // Concurrently: send 42 into the pipe, and receive from the pipe.
            Join(pipe->sender.Push(42),
                 Map(pipe->receiver.Next(),
                     [](NextResult<int> r) { return r.value(); })),
            // Once complete, verify successful sending and the received value
            // is 42.
            [](std::tuple<bool, int> result) {
              EXPECT_TRUE(std::get<0>(result));
              EXPECT_EQ(42, std::get<1>(result));
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

TEST_F(PipeTest, CanInterceptAndMapAtSender) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        auto* pipe = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        pipe->sender.InterceptAndMap([](int value) { return value / 2; });
        return Seq(
            // Concurrently: send 42 into the pipe, and receive from the pipe.
            Join(pipe->sender.Push(42),
                 Map(pipe->receiver.Next(),
                     [](NextResult<int> r) { return r.value(); })),
            // Once complete, verify successful sending and the received value
            // is 21.
            [](std::tuple<bool, int> result) {
              EXPECT_TRUE(std::get<0>(result));
              EXPECT_EQ(21, std::get<1>(result));
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

TEST_F(PipeTest, CanInterceptAndMapAtReceiver) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        auto* pipe = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        pipe->receiver.InterceptAndMap([](int value) { return value / 2; });
        return Seq(
            // Concurrently: send 42 into the pipe, and receive from the pipe.
            Join(pipe->sender.Push(42),
                 Map(pipe->receiver.Next(),
                     [](NextResult<int> r) { return r.value(); })),
            // Once complete, verify successful sending and the received value
            // is 21.
            [](std::tuple<bool, int> result) {
              EXPECT_TRUE(std::get<0>(result));
              EXPECT_EQ(21, std::get<1>(result));
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

TEST_F(PipeTest, InterceptionOrderingIsCorrect) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        auto* pipe = GetContext<Arena>()->ManagedNew<Pipe<std::string>>();
        auto appender = [](char c) {
          return [c](std::string value) {
            value += c;
            return value;
          };
        };
        // Interception get added outwardly from the center, and run from sender
        // to receiver, so the following should result in append "abcd".
        pipe->receiver.InterceptAndMap(appender('c'));
        pipe->sender.InterceptAndMap(appender('b'));
        pipe->receiver.InterceptAndMap(appender('d'));
        pipe->sender.InterceptAndMap(appender('a'));
        return Seq(
            // Concurrently: send "" into the pipe, and receive from the pipe.
            Join(pipe->sender.Push(""),
                 Map(pipe->receiver.Next(),
                     [](NextResult<std::string> r) { return r.value(); })),
            // Once complete, verify successful sending and the received value
            // is 21.
            [](std::tuple<bool, std::string> result) {
              EXPECT_TRUE(std::get<0>(result));
              EXPECT_EQ("abcd", std::get<1>(result));
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

TEST_F(PipeTest, CanReceiveAndSend) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        auto* pipe = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        return Seq(
            // Concurrently: receive from the pipe, and send 42 into the pipe.
            Join(Map(pipe->receiver.Next(),
                     [](NextResult<int> r) { return r.value(); }),
                 pipe->sender.Push(42)),
            // Once complete, verify the received value is 42 and successful
            // sending.
            [](std::tuple<int, bool> result) {
              EXPECT_EQ(std::get<0>(result), 42);
              EXPECT_TRUE(std::get<1>(result));
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

TEST_F(PipeTest, CanSeeClosedOnSend) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        Pipe<int> pipe;
        auto sender = std::move(pipe.sender);
        auto receiver = std::make_shared<std::unique_ptr<PipeReceiver<int>>>(
            std::make_unique<PipeReceiver<int>>(std::move(pipe.receiver)));
        return Seq(
            // Concurrently:
            // - push 43 into the sender, which will stall because there is no
            //   reader
            // - and close the receiver, which will fail the pending send.
            Join(sender.Push(43),
                 [receiver] {
                   receiver->reset();
                   return absl::OkStatus();
                 }),
            // Verify both that the send failed and that we executed the close.
            [](const std::tuple<bool, absl::Status>& result) {
              EXPECT_EQ(result, std::make_tuple(false, absl::OkStatus()));
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

TEST_F(PipeTest, CanSeeClosedOnReceive) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        Pipe<int> pipe;
        auto sender = std::make_shared<std::unique_ptr<PipeSender<int>>>(
            std::make_unique<PipeSender<int>>(std::move(pipe.sender)));
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
            [](std::tuple<NextResult<int>, absl::Status> result) {
              EXPECT_FALSE(std::get<0>(result).has_value());
              EXPECT_EQ(std::get<1>(result), absl::OkStatus());
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

TEST_F(PipeTest, CanCloseSend) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        auto* pipe = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        return Seq(
            // Concurrently:
            // - wait for a received value (will stall forever since we push
            //   nothing into the queue)
            // - close the sender, which will signal the receiver to return an
            //   end-of-stream.
            Join(pipe->receiver.Next(),
                 [pipe]() mutable {
                   pipe->sender.Close();
                   return absl::OkStatus();
                 }),
            // Verify we received end-of-stream and closed the sender.
            [](std::tuple<NextResult<int>, absl::Status> result) {
              EXPECT_FALSE(std::get<0>(result).has_value());
              EXPECT_FALSE(std::get<0>(result).cancelled());
              EXPECT_EQ(std::get<1>(result), absl::OkStatus());
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

TEST_F(PipeTest, CanCloseWithErrorSend) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        auto* pipe = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        return Seq(
            // Concurrently:
            // - wait for a received value (will stall forever since we push
            //   nothing into the queue)
            // - close the sender, which will signal the receiver to return an
            //   end-of-stream.
            Join(pipe->receiver.Next(),
                 [pipe]() mutable {
                   pipe->sender.CloseWithError();
                   return absl::OkStatus();
                 }),
            // Verify we received end-of-stream and closed the sender.
            [](std::tuple<NextResult<int>, absl::Status> result) {
              EXPECT_FALSE(std::get<0>(result).has_value());
              EXPECT_TRUE(std::get<0>(result).cancelled());
              EXPECT_EQ(std::get<1>(result), absl::OkStatus());
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

TEST_F(PipeTest, CanCloseWithErrorRecv) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        auto* pipe = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        return Seq(
            // Concurrently:
            // - wait for a received value (will stall forever since we push
            //   nothing into the queue)
            // - close the sender, which will signal the receiver to return an
            //   end-of-stream.
            Join(pipe->receiver.Next(),
                 [pipe]() mutable {
                   pipe->receiver.CloseWithError();
                   return absl::OkStatus();
                 }),
            // Verify we received end-of-stream and closed the sender.
            [](std::tuple<NextResult<int>, absl::Status> result) {
              EXPECT_FALSE(std::get<0>(result).has_value());
              EXPECT_TRUE(std::get<0>(result).cancelled());
              EXPECT_EQ(std::get<1>(result), absl::OkStatus());
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

TEST_F(PipeTest, CanCloseSendWithInterceptor) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        auto* pipe = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        pipe->sender.InterceptAndMap([](int value) { return value + 1; });
        return Seq(
            // Concurrently:
            // - wait for a received value (will stall forever since we push
            //   nothing into the queue)
            // - close the sender, which will signal the receiver to return an
            //   end-of-stream.
            Join(pipe->receiver.Next(),
                 [pipe]() mutable {
                   pipe->sender.Close();
                   return absl::OkStatus();
                 }),
            // Verify we received end-of-stream and closed the sender.
            [](std::tuple<NextResult<int>, absl::Status> result) {
              EXPECT_FALSE(std::get<0>(result).has_value());
              EXPECT_FALSE(std::get<0>(result).cancelled());
              EXPECT_EQ(std::get<1>(result), absl::OkStatus());
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

TEST_F(PipeTest, CanCancelSendWithInterceptor) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        auto* pipe = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        pipe->sender.InterceptAndMap([](int) { return absl::nullopt; });
        return Seq(
            // Concurrently:
            // - wait for a received value (will stall forever since we push
            //   nothing into the queue)
            // - close the sender, which will signal the receiver to return an
            //   end-of-stream.
            Join(pipe->receiver.Next(), pipe->sender.Push(3)),
            // Verify we received end-of-stream with cancellation and sent
            // successfully.
            [](std::tuple<NextResult<int>, bool> result) {
              EXPECT_FALSE(std::get<0>(result).has_value());
              EXPECT_TRUE(std::get<0>(result).cancelled());
              EXPECT_FALSE(std::get<1>(result));
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

TEST_F(PipeTest, CanFlowControlThroughManyStages) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  auto done = std::make_shared<bool>(false);
  // Push a value through multiple pipes.
  // Ensure that it's possible to do so and get flow control throughout the
  // entire pipe: ie that the push down does not complete until the last pipe
  // completes.
  MakeActivity(
      [done] {
        auto* pipe1 = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        auto* pipe2 = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        auto* pipe3 = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        auto* sender1 = &pipe1->sender;
        auto* receiver1 = &pipe1->receiver;
        auto* sender2 = &pipe2->sender;
        auto* receiver2 = &pipe2->receiver;
        auto* sender3 = &pipe3->sender;
        auto* receiver3 = &pipe3->receiver;
        return Seq(Join(Seq(sender1->Push(1),
                            [done] {
                              *done = true;
                              return 1;
                            }),
                        Seq(receiver1->Next(),
                            [sender2](NextResult<int> r) mutable {
                              return sender2->Push(r.value());
                            }),
                        Seq(receiver2->Next(),
                            [sender3](NextResult<int> r) mutable {
                              return sender3->Push(r.value());
                            }),
                        Seq(receiver3->Next(),
                            [done](NextResult<int> r) {
                              EXPECT_EQ(r.value(), 1);
                              EXPECT_FALSE(*done);
                              return 2;
                            })),
                   [](std::tuple<int, bool, bool, int> result) {
                     EXPECT_EQ(result, std::make_tuple(1, true, true, 2));
                     return absl::OkStatus();
                   });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
  ASSERT_TRUE(*done);
}

TEST_F(PipeTest, AwaitClosedWorks) {
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [] {
        auto* pipe = GetContext<Arena>()->ManagedNew<Pipe<int>>();
        pipe->sender.InterceptAndMap([](int value) { return value + 1; });
        return Seq(
            // Concurrently:
            // - wait for closed on both ends
            // - close the sender, which will signal the receiver to return an
            //   end-of-stream.
            Join(pipe->receiver.AwaitClosed(), pipe->sender.AwaitClosed(),
                 [pipe]() mutable {
                   pipe->sender.Close();
                   return absl::OkStatus();
                 }),
            // Verify we received end-of-stream and closed the sender.
            [](std::tuple<bool, bool, absl::Status> result) {
              EXPECT_FALSE(std::get<0>(result));
              EXPECT_FALSE(std::get<1>(result));
              EXPECT_EQ(std::get<2>(result), absl::OkStatus());
              return absl::OkStatus();
            });
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      MakeScopedArena(1024, &memory_allocator_));
}

class FakeActivity final : public Activity {
 public:
  void Orphan() override {}
  void ForceImmediateRepoll(WakeupMask) override {}
  Waker MakeOwningWaker() override { Crash("Not implemented"); }
  Waker MakeNonOwningWaker() override { Crash("Not implemented"); }
  void Run(absl::FunctionRef<void()> f) {
    ScopedActivity activity(this);
    f();
  }
};

TEST_F(PipeTest, PollAckWaitsForReadyClosed) {
  FakeActivity().Run([]() {
    pipe_detail::Center<int> c;
    int i = 1;
    EXPECT_EQ(c.Push(&i), Poll<bool>(true));
    c.MarkClosed();
    EXPECT_EQ(c.PollAck(), Poll<bool>(Pending{}));
  });
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
