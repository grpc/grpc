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
    return i < 5;
  })();
  EXPECT_EQ(i, 5);
}

TEST(WhileTest, CountToFiveWithResult) {
  int i = 0;
  auto j = While([&i]() -> Poll<absl::optional<int>> {
    i++;
    return i < 5 ? absl::optional<int>{} : absl::optional<int>{i};
  })();
  EXPECT_EQ(j, Poll<int>(5));
}

TEST(WhileTest, CountToFiveWithStatus) {
  int i = 0;
  EXPECT_TRUE(absl::get<kPollReadyIdx>(While([&i]() {
                i++;
                return absl::StatusOr<bool>(i < 5);
              })())
                  .ok());
  EXPECT_EQ(i, 5);
}

TEST(WhileTest, CountToFiveWithStatusAndResult) {
  int i = 0;
  EXPECT_EQ(*absl::get<kPollReadyIdx>(While([&i]() {
    i++;
    return absl::StatusOr<absl::optional<int>>(i < 5 ? absl::optional<int>{}
                                                     : absl::optional<int>{i});
  })()),
            5);
}

TEST(WhileTest, Failure) {
  EXPECT_FALSE(
      absl::get<kPollReadyIdx>(While([]() { return absl::StatusOr<bool>(); })())
          .ok());
}

TEST(WhileTest, FailureWithResult) {
  EXPECT_FALSE(absl::get<kPollReadyIdx>(While([]() {
                 return absl::StatusOr<absl::optional<int>>();
               })())
                   .ok());
}

TEST(WhileTest, FactoryCountToFive) {
  int i = 0;
  While([&i]() {
    return [&i]() {
      i++;
      return i < 5;
    };
  })();
  EXPECT_EQ(i, 5);
}

TEST(WhileTest, FactoryCountToFiveWithResult) {
  int i = 0;
  auto j = While([&i]() {
    return [&i]() -> Poll<absl::optional<int>> {
      i++;
      return i < 5 ? absl::optional<int>{} : absl::optional<int>{i};
    };
  })();
  EXPECT_EQ(j, Poll<int>(5));
}

TEST(WhileTest, FactoryCountToFiveWithStatus) {
  int i = 0;
  EXPECT_TRUE(absl::get<kPollReadyIdx>(While([&i]() {
                return [&i]() {
                  i++;
                  return absl::StatusOr<bool>(i < 5);
                };
              })())
                  .ok());
  EXPECT_EQ(i, 5);
}

TEST(WhileTest, FactoryCountToFiveWithStatusAndResult) {
  int i = 0;
  EXPECT_EQ(*absl::get<kPollReadyIdx>(While([&i]() {
    return [&i]() {
      i++;
      return absl::StatusOr<absl::optional<int>>(
          i < 5 ? absl::optional<int>{} : absl::optional<int>{i});
    };
  })()),
            5);
}

TEST(WhileTest, FactoryFailure) {
  EXPECT_FALSE(absl::get<kPollReadyIdx>(While([]() {
                 return []() { return absl::StatusOr<bool>(); };
               })())
                   .ok());
}

TEST(WhileTest, FactoryFailureWithResult) {
  EXPECT_FALSE(absl::get<kPollReadyIdx>(While([]() {
                 return []() { return absl::StatusOr<absl::optional<int>>(); };
               })())
                   .ok());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
