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

#include "src/core/lib/promise/promise.h"

#include "gtest/gtest.h"

namespace grpc_core {

TEST(PromiseTest, Works) {
  Promise<int> x = []() { return 42; };
  EXPECT_EQ(x(), Poll<int>(42));
}

TEST(PromiseTest, Immediate) { EXPECT_EQ(Immediate(42)(), Poll<int>(42)); }

TEST(PromiseTest, WithResult) {
  EXPECT_EQ(WithResult<int>(Immediate(42))(), Poll<int>(42));
  // Fails to compile: WithResult<int>(Immediate(std::string("hello")));
  // Fails to compile: WithResult<int>(Immediate(42.9));
}

TEST(PromiseTest, NowOrNever) {
  EXPECT_EQ(NowOrNever(Immediate(42)), absl::optional<int>(42));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
