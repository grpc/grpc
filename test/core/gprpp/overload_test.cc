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

#include "src/core/lib/gprpp/overload.h"

#include <string>

#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {

TEST(Overload, Test) {
  auto a = [](int x) { return x; };
  auto b = [](std::string x) -> int { return x.length(); };
  auto overload = Overload(a, b);
  EXPECT_EQ(overload(1), 1);
  EXPECT_EQ(overload("1"), 1);
  EXPECT_EQ(overload("abc"), 3);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
