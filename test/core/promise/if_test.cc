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

#include "gtest/gtest.h"

namespace grpc_core {

TEST(IfTest, ChooseTrue) {
  int execution_order = 0;
  EXPECT_EQ(If(
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 1;
                  return true;
                },
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 2;
                  return 2;
                },
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 3;
                  return 3;
                })(),
            Poll<int>(2));
  EXPECT_EQ(execution_order, 12);
}

TEST(IfTest, ChooseFalse) {
  int execution_order = 0;
  EXPECT_EQ(If(
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 1;
                  return false;
                },
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 2;
                  return 2;
                },
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 3;
                  return 3;
                })(),
            Poll<int>(3));
  EXPECT_EQ(execution_order, 13);
}

TEST(IfTest, ChooseSuccessfulTrue) {
  int execution_order = 0;
  EXPECT_EQ(If(
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 1;
                  return absl::StatusOr<bool>(true);
                },
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 2;
                  return absl::StatusOr<int>(2);
                },
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 3;
                  return absl::StatusOr<int>(3);
                })(),
            Poll<absl::StatusOr<int>>(absl::StatusOr<int>(2)));
  EXPECT_EQ(execution_order, 12);
}

TEST(IfTest, ChooseSuccessfulFalse) {
  int execution_order = 0;
  EXPECT_EQ(If(
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 1;
                  return absl::StatusOr<bool>(false);
                },
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 2;
                  return absl::StatusOr<int>(2);
                },
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 3;
                  return absl::StatusOr<int>(3);
                })(),
            Poll<absl::StatusOr<int>>(absl::StatusOr<int>(3)));
  EXPECT_EQ(execution_order, 13);
}

TEST(IfTest, ChooseFailure) {
  int execution_order = 0;
  EXPECT_EQ(If(
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 1;
                  return absl::StatusOr<bool>();
                },
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 2;
                  return absl::StatusOr<int>(2);
                },
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 3;
                  return absl::StatusOr<int>(3);
                })(),
            Poll<absl::StatusOr<int>>(absl::StatusOr<int>()));
  EXPECT_EQ(execution_order, 1);
}

TEST(IfTest, ChoosePending) {
  int execution_order = 0;
  int once = false;
  auto if_combiner = If(
      [&execution_order, &once]() -> Poll<bool> {
        execution_order = (10 * execution_order) + 1;
        if (once) return true;
        once = true;
        return Pending{};
      },
      [&execution_order]() {
        execution_order = (10 * execution_order) + 2;
        return 2;
      },
      [&execution_order]() {
        execution_order = (10 * execution_order) + 3;
        return 3;
      });

  Poll<int> first_execution = if_combiner();
  EXPECT_FALSE(first_execution.ready());
  EXPECT_EQ(execution_order, 1);

  execution_order = 0;
  Poll<int> second_execution = if_combiner();
  EXPECT_TRUE(second_execution.ready());
  EXPECT_EQ(second_execution.value(), 2);
  EXPECT_EQ(execution_order, 12);
}

TEST(IfTest, ImmediateChooseTrue) {
  int execution_order = 0;
  EXPECT_EQ(If(
                true,
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 2;
                  return 2;
                },
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 3;
                  return 3;
                })(),
            Poll<int>(2));
  EXPECT_EQ(execution_order, 2);
}

TEST(IfTest, ImmediateChooseFalse) {
  int execution_order = 0;
  EXPECT_EQ(If(
                false,
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 2;
                  return 2;
                },
                [&execution_order]() {
                  execution_order = (10 * execution_order) + 3;
                  return 3;
                })(),
            Poll<int>(3));
  EXPECT_EQ(execution_order, 3);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
