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

#include "src/core/lib/promise/inter_activity_pipe.h"

#include <memory>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/seq.h"
#include "test/core/promise/test_wakeup_schedulers.h"

namespace grpc_core {

template <typename F>
ActivityPtr TestActivity(F f) {
  return MakeActivity(std::move(f), InlineWakeupScheduler{},
                      [](absl::Status status) { EXPECT_TRUE(status.ok()); });
}

TEST(InterActivityPipe, CanSendAndReceive) {
  InterActivityPipe<int, 1> pipe;
  bool done = false;
  auto a = TestActivity(Seq(pipe.sender.Push(3), [](bool b) {
    EXPECT_TRUE(b);
    return absl::OkStatus();
  }));
  EXPECT_FALSE(done);
  auto b = TestActivity(Seq(pipe.receiver.Next(),
                            [&done](InterActivityPipe<int, 1>::NextResult n) {
                              EXPECT_EQ(n.value(), 3);
                              done = true;
                              return absl::OkStatus();
                            }));
  EXPECT_TRUE(done);
}

TEST(InterActivityPipe, CanSendTwiceAndReceive) {
  InterActivityPipe<int, 1> pipe;
  bool done = false;
  auto a = TestActivity(Seq(
      pipe.sender.Push(3),
      [&](bool b) {
        EXPECT_TRUE(b);
        return pipe.sender.Push(4);
      },
      [](bool b) {
        EXPECT_TRUE(b);
        return absl::OkStatus();
      }));
  EXPECT_FALSE(done);
  auto b = TestActivity(Seq(
      pipe.receiver.Next(),
      [&pipe](InterActivityPipe<int, 1>::NextResult n) {
        EXPECT_EQ(n.value(), 3);
        return pipe.receiver.Next();
      },
      [&done](InterActivityPipe<int, 1>::NextResult n) {
        EXPECT_EQ(n.value(), 4);
        done = true;
        return absl::OkStatus();
      }));
  EXPECT_TRUE(done);
}

TEST(InterActivityPipe, CanReceiveAndSend) {
  InterActivityPipe<int, 1> pipe;
  bool done = false;
  auto b = TestActivity(Seq(pipe.receiver.Next(),
                            [&done](InterActivityPipe<int, 1>::NextResult n) {
                              EXPECT_EQ(n.value(), 3);
                              done = true;
                              return absl::OkStatus();
                            }));
  EXPECT_FALSE(done);
  auto a = TestActivity(Seq(pipe.sender.Push(3), [](bool b) {
    EXPECT_TRUE(b);
    return absl::OkStatus();
  }));
  EXPECT_TRUE(done);
}

TEST(InterActivityPipe, CanClose) {
  InterActivityPipe<int, 1> pipe;
  bool done = false;
  auto b = TestActivity(Seq(pipe.receiver.Next(),
                            [&done](InterActivityPipe<int, 1>::NextResult n) {
                              EXPECT_FALSE(n.has_value());
                              done = true;
                              return absl::OkStatus();
                            }));
  EXPECT_FALSE(done);
  // Drop the sender
  {
    auto x = std::move(pipe.sender);
  }
  EXPECT_TRUE(done);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
