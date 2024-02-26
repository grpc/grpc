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

#include "src/core/lib/promise/status_flag.h"

#include <memory>
#include <utility>

#include "gtest/gtest.h"

namespace grpc_core {

TEST(StatusFlagTest, Basics) {
  EXPECT_TRUE(StatusFlag(true).ok());
  EXPECT_FALSE(StatusFlag(false).ok());
  EXPECT_TRUE(StatusCast<absl::Status>(StatusFlag(true)).ok());
  EXPECT_FALSE(StatusCast<absl::Status>(StatusFlag(false)).ok());
  EXPECT_TRUE(ValueOrFailure<int>(42).ok());
  EXPECT_FALSE(ValueOrFailure<int>(Failure{}).ok());
  EXPECT_TRUE(StatusCast<absl::Status>(ValueOrFailure<int>(42).status()).ok());
  EXPECT_FALSE(
      StatusCast<absl::Status>(ValueOrFailure<int>(Failure{}).status()).ok());
  EXPECT_EQ(ValueOrFailure<int>(42).value(), 42);
  EXPECT_EQ(StatusCast<absl::StatusOr<int>>(ValueOrFailure<int>(42)).value(),
            42);
  EXPECT_TRUE(IsStatusOk(Success{}));
  EXPECT_FALSE(IsStatusOk(Failure{}));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
