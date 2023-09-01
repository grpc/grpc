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
  EXPECT_EQ(If([]() { return true; }, []() { return 1; }, []() { return 2; })(),
            Poll<int>(1));
}

TEST(IfTest, ChooseFalse) {
  EXPECT_EQ(
      If([]() { return false; }, []() { return 1; }, []() { return 2; })(),
      Poll<int>(2));
}

TEST(IfTest, ChooseSuccesfulTrue) {
  EXPECT_EQ(If([]() { return absl::StatusOr<bool>(true); },
               []() { return absl::StatusOr<int>(1); },
               []() { return absl::StatusOr<int>(2); })(),
            Poll<absl::StatusOr<int>>(absl::StatusOr<int>(1)));
}

TEST(IfTest, ChooseSuccesfulFalse) {
  EXPECT_EQ(If([]() { return absl::StatusOr<bool>(false); },
               []() { return absl::StatusOr<int>(1); },
               []() { return absl::StatusOr<int>(2); })(),
            Poll<absl::StatusOr<int>>(absl::StatusOr<int>(2)));
}

TEST(IfTest, ChooseFailure) {
  EXPECT_EQ(If([]() { return absl::StatusOr<bool>(); },
               []() { return absl::StatusOr<int>(1); },
               []() { return absl::StatusOr<int>(2); })(),
            Poll<absl::StatusOr<int>>(absl::StatusOr<int>()));
}

TEST(IfTest, ImmediateChooseTrue) {
  EXPECT_EQ(If(
                true, []() { return 1; }, []() { return 2; })(),
            Poll<int>(1));
}

TEST(IfTest, ImmediateChooseFalse) {
  EXPECT_EQ(If(
                false, []() { return 1; }, []() { return 2; })(),
            Poll<int>(2));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
