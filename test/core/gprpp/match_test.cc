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

#include "src/core/lib/gprpp/match.h"
#include <gtest/gtest.h>

namespace grpc_core {
namespace testing {

TEST(MatchTest, Test) {
  EXPECT_EQ(Match(
                absl::variant<int, double>(1.9), [](int x) -> int { abort(); },
                [](double x) -> int {
                  EXPECT_EQ(x, 1.9);
                  return 42;
                }),
            42);
  EXPECT_EQ(Match(
                absl::variant<int, double>(3),
                [](int x) -> int {
                  EXPECT_EQ(x, 3);
                  return 42;
                },
                [](double x) -> int { abort(); }),
            42);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
