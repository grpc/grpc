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

#include "src/core/lib/promise/if.h"

#include "src/core/lib/promise/poll.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {

TEST(IfTest, ChooseTrue) {
  std::string execution_order;
  auto if_combiner = If(
      [&execution_order]() {
        absl::StrAppend(&execution_order, 1);
        return true;
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, 2);
        return 2;
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, "UnreachedFalse");
        return 3;
      });
  EXPECT_EQ(if_combiner(), Poll<int>(2));
  EXPECT_STREQ(execution_order.c_str(), "12");
}

TEST(IfTest, ChooseFalse) {
  std::string execution_order;
  auto if_combiner = If(
      [&execution_order]() {
        absl::StrAppend(&execution_order, 1);
        return false;
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, "UnreachableTrue");
        return 2;
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, 3);
        return 3;
      });
  EXPECT_EQ(if_combiner(), Poll<int>(3));
  EXPECT_STREQ(execution_order.c_str(), "13");
}

TEST(IfTest, ChooseSuccessfulTrue) {
  std::string execution_order;
  auto if_combiner = If(
      [&execution_order]() {
        absl::StrAppend(&execution_order, 1);
        return absl::StatusOr<bool>(true);
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, 2);
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, "UnreachableFalse");
        return absl::StatusOr<int>(3);
      });
  EXPECT_EQ(if_combiner(), Poll<absl::StatusOr<int>>(absl::StatusOr<int>(2)));
  EXPECT_STREQ(execution_order.c_str(), "12");
}

TEST(IfTest, ChooseSuccessfulFalse) {
  std::string execution_order;
  auto if_combiner = If(
      [&execution_order]() {
        absl::StrAppend(&execution_order, 1);
        return absl::StatusOr<bool>(false);
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, "UnreachableTrue");
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, 3);
        return absl::StatusOr<int>(3);
      });
  EXPECT_EQ(if_combiner(), Poll<absl::StatusOr<int>>(absl::StatusOr<int>(3)));
  EXPECT_STREQ(execution_order.c_str(), "13");
}

TEST(IfTest, ChooseFailure) {
  std::string execution_order;
  auto if_combiner = If(
      [&execution_order]() {
        absl::StrAppend(&execution_order, 1);
        return absl::StatusOr<bool>();
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, "UnreachableTrue");
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, "UnreachableFalse");
        return absl::StatusOr<int>(3);
      });
  EXPECT_EQ(if_combiner(), Poll<absl::StatusOr<int>>(absl::StatusOr<int>()));
  EXPECT_STREQ(execution_order.c_str(), "1");
}

TEST(IfTest, ChoosePending) {
  std::string execution_order;
  int once = false;
  auto if_combiner = If(
      [&execution_order, &once]() -> Poll<bool> {
        absl::StrAppend(&execution_order, 1);
        if (once) return true;
        once = true;
        return Pending{};
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, 2);
        return 2;
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, "UnreachableFalse");
        return 3;
      });

  Poll<int> first_execution = if_combiner();
  EXPECT_FALSE(first_execution.ready());
  EXPECT_STREQ(execution_order.c_str(), "1");

  execution_order.clear();
  Poll<int> second_execution = if_combiner();
  EXPECT_TRUE(second_execution.ready());
  EXPECT_EQ(second_execution.value(), 2);
  EXPECT_EQ(execution_order, "12");
}

TEST(IfTest, ImmediateChooseTrue) {
  std::string execution_order;
  auto if_combiner = If(
      true,
      [&execution_order]() {
        absl::StrAppend(&execution_order, 2);
        return 2;
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, "UnreachableFalse");
        return 3;
      });
  EXPECT_EQ(if_combiner(), Poll<int>(2));
  EXPECT_STREQ(execution_order.c_str(), "2");
}

TEST(IfTest, ImmediateChooseFalse) {
  std::string execution_order;
  auto if_combiner = If(
      false,
      [&execution_order]() {
        absl::StrAppend(&execution_order, "UnreachableTrue");
        return 2;
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, 3);
        return 3;
      });
  EXPECT_EQ(if_combiner(), Poll<int>(3));
  EXPECT_STREQ(execution_order.c_str(), "3");
}

TEST(IfTest, NestedIfChooseTrueThenFalse) {
  std::string execution_order;
  auto nested_if_combiner = If(
      [&execution_order]() {
        absl::StrAppend(&execution_order, 1);
        return true;
      },
      // Outer True Branch (Nested If)
      [&execution_order]() {
        absl::StrAppend(&execution_order, 2);
        return If(
            [&execution_order]() {
              absl::StrAppend(&execution_order, 3);
              return false;
            },
            [&execution_order]() {
              absl::StrAppend(&execution_order, "UnreachableInnerTrue");
              return 4;
            },
            [&execution_order]() {
              absl::StrAppend(&execution_order, 5);
              return 5;
            });
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, "UnreachableOuterFalse");
        return 6;
      });
  EXPECT_EQ(nested_if_combiner(), Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "1235");
}

TEST(IfTest, NestedIfWithPendingState) {
  std::string execution_order;
  bool inner_ready = false;

  auto nested_if_combiner = If(
      [&execution_order]() {
        absl::StrAppend(&execution_order, 1);
        return true;
      },
      [&execution_order, &inner_ready]() {
        absl::StrAppend(&execution_order, 2);
        return If(
            // Inner condition returns Pending on first poll, True on second
            [&execution_order, &inner_ready]() -> Poll<bool> {
              if (!inner_ready) {
                inner_ready = true;
                absl::StrAppend(&execution_order, "3a");
                return Pending{};
              }
              absl::StrAppend(&execution_order, "3b");
              return true;
            },
            [&execution_order]() {
              absl::StrAppend(&execution_order, 4);
              return 4;
            },
            [&execution_order]() {
              absl::StrAppend(&execution_order, "UnreachableInnerFalse");
              return 5;
            });
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, "UnreachableOuterFalse");
        return 6;
      });

  EXPECT_FALSE(nested_if_combiner().ready());
  EXPECT_STREQ(execution_order.c_str(), "123a");
  execution_order.clear();

  Poll<int> final_execution = nested_if_combiner();
  EXPECT_TRUE(final_execution.ready());
  EXPECT_EQ(final_execution.value(), 4);

  // Resumes at Inner Cond (3b) -> Inner True Branch (4)
  EXPECT_STREQ(execution_order.c_str(), "3b4");
}

TEST(IfTest, NestedIfOuterFalseIgnoresInner) {
  std::string execution_order;
  auto nested_if_combiner = If(
      [&execution_order]() {
        absl::StrAppend(&execution_order, 1);
        return false;
      },
      // Outer True Branch (Nested if should be ignored)
      [&execution_order]() {
        absl::StrAppend(&execution_order, "UnreachableOuterTrue");
        return If(
            [&execution_order]() {
              absl::StrAppend(&execution_order, "UnreachableInnerCond");
              return true;
            },
            []() { return 2; }, []() { return 3; });
      },
      [&execution_order]() {
        absl::StrAppend(&execution_order, 4);
        return 4;
      });
  EXPECT_EQ(nested_if_combiner(), Poll<int>(4));
  EXPECT_STREQ(execution_order.c_str(), "14");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
