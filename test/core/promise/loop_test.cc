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

#include "src/core/lib/promise/loop.h"

#include <memory>
#include <utility>

#include "src/core/lib/promise/seq.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {

TEST(LoopTest, CountToFive) {
  std::string execution_order;
  int i = 0;
  Poll<int> retval = Loop([&execution_order, &i]() {
    return [&execution_order, &i]() -> LoopCtl<int> {
      absl::StrAppend(&execution_order, i);
      i++;
      if (i < 5) return Continue();
      return i;
    };
  })();
  EXPECT_TRUE(retval.ready());
  EXPECT_EQ(retval.value(), 5);
  EXPECT_EQ(i, 5);
  EXPECT_STREQ(execution_order.c_str(), "01234");
}

TEST(LoopTest, CountToFivePoll) {
  std::string execution_order;
  int i = 0;
  Poll<int> retval = Loop([&execution_order, &i]() {
    return [&execution_order, &i]() -> Poll<LoopCtl<int>> {
      absl::StrAppend(&execution_order, i);
      i++;
      if (i == 5) {
        absl::StrAppend(&execution_order, "P");
        return Pending{};
      };
      return Continue();
    };
  })();
  EXPECT_TRUE(retval.pending());
  EXPECT_EQ(i, 5);
  EXPECT_STREQ(execution_order.c_str(), "01234P");
}

TEST(LoopTest, FactoryCountToFive) {
  std::string execution_order;
  int i = 0;
  Poll<int> retval = Loop([&execution_order, &i]() {
    return [&execution_order, &i]() -> LoopCtl<int> {
      absl::StrAppend(&execution_order, i);
      i++;
      if (i < 5) return Continue();
      return i;
    };
  })();
  EXPECT_TRUE(retval.ready());
  EXPECT_EQ(retval.value(), 5);
  EXPECT_STREQ(execution_order.c_str(), "01234");
  EXPECT_EQ(i, 5);
}

TEST(LoopTest, LoopOfSeq) {
  std::string execution_order;
  Poll<int> retval = Loop([&execution_order]() {
    return Seq(
        [&execution_order]() mutable -> Poll<int> {
          absl::StrAppend(&execution_order, "a");
          return 42;
        },
        [&execution_order](int i) mutable -> LoopCtl<int> {
          absl::StrAppend(&execution_order, i);
          return i;
        });
  })();
  EXPECT_TRUE(retval.ready());
  EXPECT_EQ(retval, Poll<int>(42));
  EXPECT_STREQ(execution_order.c_str(), "a42");
}

TEST(LoopTest, LoopOfSeqMultiple) {
  std::string execution_order;
  Poll<int> retval = Loop([&execution_order]() {
    return Seq(
        [&execution_order]() mutable -> Poll<int> {
          absl::StrAppend(&execution_order, "a");
          return execution_order.length();
        },
        [&execution_order](int i) mutable -> LoopCtl<int> {
          absl::StrAppend(&execution_order, i);
          if (i < 9) return Continue();
          return i;
        });
  })();
  EXPECT_TRUE(retval.ready());
  EXPECT_EQ(retval, Poll<int>(9));
  EXPECT_STREQ(execution_order.c_str(), "a1a3a5a7a9");
}

TEST(LoopTest, CanAccessFactoryLambdaVariables) {
  std::string execution_order;
  int i = 99;
  auto x = Loop([&execution_order, p = &i]() {
    return [q = &p, &execution_order]() -> Poll<LoopCtl<int>> {
      absl::StrAppend(&execution_order, **q);
      ++**q;
      return Pending{};
    };
  });
  auto y = std::move(x);
  auto z = std::move(y);
  Poll<int> retval = z();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "99");
  EXPECT_EQ(i, 100);
}

TEST(LoopTest, NTimes) {
  std::string execution_order;
  auto x = NTimes(3, [&execution_order](int i) {
    return [&execution_order, i]() {
      absl::StrAppend(&execution_order, i);
      return Empty{};
    };
  });
  while (x().pending()) {
  }
  EXPECT_EQ(execution_order, "012");
}

TEST(LoopTest, YieldAndResume) {
  std::string execution_order;
  int poll_count = 0;
  int i = 0;
  auto loop = Loop([&execution_order, &poll_count, &i]() {
    absl::StrAppend(&execution_order, "F");  // Factory called
    return [&execution_order, &poll_count, &i]() -> Poll<LoopCtl<int>> {
      poll_count++;
      // Yield `Pending` on every odd poll.
      if (poll_count % 2 != 0) {
        absl::StrAppend(&execution_order, "P");
        return Pending{};
      }
      // On even polls, complete the iteration
      i++;
      if (i < 3) {
        absl::StrAppend(&execution_order, i);
        return Continue();
      }
      absl::StrAppend(&execution_order, i);
      return i;  // Break and return 3
    };
  });

  // 1st Poll: Pending
  EXPECT_TRUE(loop().pending());
  // 2nd Poll: Continues(i = 1)
  // triggers next factory call which returns Pending.
  EXPECT_TRUE(loop().pending());
  // 3rd Poll: Continues, triggers next factory call, again returns Pending.
  EXPECT_TRUE(loop().pending());

  // 4th Poll: Breaks and returns (poll_count = 6, i = 3)
  Poll<int> result = loop();
  EXPECT_TRUE(result.ready());
  EXPECT_EQ(result.value(), 3);
  EXPECT_EQ(poll_count, 6);
  EXPECT_EQ(execution_order, "FP1FP2FP3");
}

TEST(LoopTest, BailOnError) {
  int i = 0;
  auto loop = Loop([&i]() {
    return [&i]() -> LoopCtl<absl::StatusOr<int>> {
      i++;
      if (i == 3) {
        // Simulating a failure during the 3rd iteration
        return absl::InternalError("Failed on iteration 3");
      }
      if (i < 5) {
        return Continue();
      }
      return i;  // Would return 5 if it succeeded
    };
  });

  auto result = loop();
  EXPECT_TRUE(result.ready());
  EXPECT_EQ(result.value().status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(result.value().status().message(), "Failed on iteration 3");
  EXPECT_EQ(i, 3);
}

TEST(LoopTest, NestedSequenceOfLoops) {
  int outer_sum = 0;
  int total_inner_count = 0;
  auto loop = Loop([&outer_sum, &total_inner_count]() {
    return Seq(
        // Inner Loop: Breaks every 3 iterations
        Loop([&]() {
          return [&]() -> LoopCtl<int> {
            total_inner_count++;
            if (total_inner_count % 3 != 0) return Continue();
            return 10;  // Used to increment outer_sum to test seq working.
          };
        }),
        // Outer Loop Evaluation: Gives 1 on 3rd iteration, continues otherwise.
        [&](int inner_result) -> LoopCtl<int> {
          outer_sum += inner_result;
          if (outer_sum < 30) return Continue();
          return 1;  // Break outer loop
        });
  });

  auto result = loop();
  EXPECT_TRUE(result.ready());
  EXPECT_EQ(result.value(), 1);
  EXPECT_EQ(outer_sum, 30);
  EXPECT_EQ(total_inner_count, 9);  // 3 outer * 3 inner
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
