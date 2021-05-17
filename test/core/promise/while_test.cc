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

#include "src/core/lib/promise/while.h"
#include <gtest/gtest.h>

namespace grpc_core {

TEST(WhileTest, CountToFive) {
  int i = 0;
  While([&i]() {
    i++;
    return ready(i < 5);
  })();
  EXPECT_EQ(i, 5);
}

TEST(WhileTest, CountToFiveWithResult) {
  int i = 0;
  auto j =
      While([&i]() -> Poll<absl::optional<int>> {
        i++;
        return ready(i < 5 ? absl::optional<int>{} : absl::optional<int>{i});
      })()
          .take();
  EXPECT_EQ(j, 5);
}

TEST(WhileTest, CountToFiveWithStatus) {
  int i = 0;
  EXPECT_TRUE(While([&i]() {
                i++;
                return ready(absl::StatusOr<bool>(i < 5));
              })()
                  .take()
                  .ok());
  EXPECT_EQ(i, 5);
}

TEST(WhileTest, CountToFiveWithStatusAndResult) {
  int i = 0;
  EXPECT_EQ(*While([&i]() {
               i++;
               return ready(absl::StatusOr<absl::optional<int>>(
                   i < 5 ? absl::optional<int>{} : absl::optional<int>{i}));
             })()
                 .take(),
            5);
}

TEST(WhileTest, Failure) {
  EXPECT_FALSE(
      While([]() { return ready(absl::StatusOr<bool>()); })().take().ok());
}

TEST(WhileTest, FailureWithResult) {
  EXPECT_FALSE(
      While([]() { return ready(absl::StatusOr<absl::optional<int>>()); })()
          .take()
          .ok());
}

TEST(WhileTest, FactoryCountToFive) {
  int i = 0;
  While([&i]() {
    return [&i]() {
      i++;
      return ready(i < 5);
    };
  })();
  EXPECT_EQ(i, 5);
}

TEST(WhileTest, FactoryCountToFiveWithResult) {
  int i = 0;
  auto j =
      While([&i]() {
        return [&i]() -> Poll<absl::optional<int>> {
          i++;
          return ready(i < 5 ? absl::optional<int>{} : absl::optional<int>{i});
        };
      })()
          .take();
  EXPECT_EQ(j, 5);
}

TEST(WhileTest, FactoryCountToFiveWithStatus) {
  int i = 0;
  EXPECT_TRUE(While([&i]() {
                return [&i]() {
                  i++;
                  return ready(absl::StatusOr<bool>(i < 5));
                };
              })()
                  .take()
                  .ok());
  EXPECT_EQ(i, 5);
}

TEST(WhileTest, FactoryCountToFiveWithStatusAndResult) {
  int i = 0;
  EXPECT_EQ(*While([&i]() {
               return [&i]() {
                 i++;
                 return ready(absl::StatusOr<absl::optional<int>>(
                     i < 5 ? absl::optional<int>{} : absl::optional<int>{i}));
               };
             })()
                 .take(),
            5);
}

TEST(WhileTest, FactoryFailure) {
  EXPECT_FALSE(
      While([]() { return []() { return ready(absl::StatusOr<bool>()); }; })()
          .take()
          .ok());
}

TEST(WhileTest, FactoryFailureWithResult) {
  EXPECT_FALSE(While([]() {
                 return []() {
                   return ready(absl::StatusOr<absl::optional<int>>());
                 };
               })()
                   .take()
                   .ok());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
