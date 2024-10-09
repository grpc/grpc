// Copyright 2022 gRPC authors.
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

#include "src/core/lib/promise/map_pipe.h"

#include <grpc/event_engine/memory_allocator.h>

#include <memory>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/promise/test_wakeup_schedulers.h"

using testing::Mock;
using testing::MockFunction;
using testing::StrictMock;

namespace grpc_core {

template <typename T>
class Delayed {
 public:
  explicit Delayed(T x) : x_(x) {}

  Poll<T> operator()() {
    GetContext<Activity>()->ForceImmediateRepoll();
    ++polls_;
    if (polls_ == 10) return std::move(x_);
    return Pending();
  }

 private:
  int polls_ = 0;
  T x_;
};

TEST(MapPipeTest, SendThriceWithPipeInterceptingReceive) {
  int num_received = 0;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [&num_received] {
        Pipe<int> pipe;
        auto filter =
            PipeMapper<int>::Intercept(pipe.receiver).TakeAndRun([](int x) {
              return Delayed<int>(x + 1);
            });
        auto sender = std::make_shared<std::unique_ptr<PipeSender<int>>>(
            std::make_unique<PipeSender<int>>(std::move(pipe.sender)));
        return Map(
            Join(
                std::move(filter),
                // Push 3 things into a pipe -- 0, 1, then 2 -- then close.
                Seq((*sender)->Push(0), [sender] { return (*sender)->Push(1); },
                    [sender] { return (*sender)->Push(2); },
                    [sender] {
                      sender->reset();
                      return absl::OkStatus();
                    }),
                // Use a ForEach loop to read them out and verify all values are
                // seen (but with 1 added).
                ForEach(std::move(pipe.receiver),
                        [&num_received](int i) {
                          num_received++;
                          EXPECT_EQ(num_received, i);
                          return absl::OkStatus();
                        })),
            JustElem<2>());
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      SimpleArenaAllocator()->MakeArena());
  Mock::VerifyAndClearExpectations(&on_done);
  EXPECT_EQ(num_received, 3);
}

TEST(MapPipeTest, SendThriceWithPipeInterceptingSend) {
  int num_received = 0;
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  MakeActivity(
      [&num_received] {
        Pipe<int> pipe;
        auto filter =
            PipeMapper<int>::Intercept(pipe.sender).TakeAndRun([](int x) {
              return Delayed<int>(x + 1);
            });
        auto sender = std::make_shared<std::unique_ptr<PipeSender<int>>>(
            std::make_unique<PipeSender<int>>(std::move(pipe.sender)));
        return Map(
            Join(
                std::move(filter),
                // Push 3 things into a pipe -- 0, 1, then 2 -- then close.
                Seq((*sender)->Push(0), [sender] { return (*sender)->Push(1); },
                    [sender] { return (*sender)->Push(2); },
                    [sender] {
                      sender->reset();
                      return absl::OkStatus();
                    }),
                // Use a ForEach loop to read them out and verify all values are
                // seen (but with 1 added).
                ForEach(std::move(pipe.receiver),
                        [&num_received](int i) {
                          num_received++;
                          EXPECT_EQ(num_received, i);
                          return absl::OkStatus();
                        })),
            JustElem<2>());
      },
      NoWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); },
      SimpleArenaAllocator()->MakeArena());
  Mock::VerifyAndClearExpectations(&on_done);
  EXPECT_EQ(num_received, 3);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
