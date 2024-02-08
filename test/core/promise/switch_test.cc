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

#include "src/core/lib/promise/switch.h"

#include "gtest/gtest.h"

namespace grpc_core {

TEST(SwitchTest, JustDefault) {
  EXPECT_EQ(Switch(42, Default([] { return 1; }))(), Poll<int>(1));
}

TEST(SwitchTest, ThreeCases) {
  auto test_switch = [](int d) {
    return Switch(d, Case(1, [] { return 25; }), Case(2, [] { return 95; }),
                  Case(3, [] { return 68; }), Default([] { return 52; }));
  };
  EXPECT_EQ(test_switch(0)(), Poll<int>(52));
  EXPECT_EQ(test_switch(1)(), Poll<int>(25));
  EXPECT_EQ(test_switch(2)(), Poll<int>(95));
  EXPECT_EQ(test_switch(3)(), Poll<int>(68));
  EXPECT_EQ(test_switch(4)(), Poll<int>(52));
}

TEST(SwitchTest, Pending) {
  auto test_switch = [](int d) {
    return Switch(d, Case(42, []() -> Poll<int> { return Pending{}; }),
                  Case(1, [] { return 25; }), Case(2, [] { return 95; }),
                  Case(3, [] { return 68; }), Default([] { return 52; }));
  };
  EXPECT_EQ(test_switch(0)(), Poll<int>(52));
  EXPECT_EQ(test_switch(1)(), Poll<int>(25));
  EXPECT_EQ(test_switch(2)(), Poll<int>(95));
  EXPECT_EQ(test_switch(3)(), Poll<int>(68));
  EXPECT_EQ(test_switch(4)(), Poll<int>(52));
  EXPECT_EQ(test_switch(42)(), Poll<int>(Pending{}));
}

TEST(SwitchTest, ThreeCasesFromEnum) {
  enum class X : uint8_t { A, B, C };

  auto test_switch = [](X d) {
    return Switch(d, Case(X::A, [] { return 25; }),
                  Case(X::B, [] { return 95; }), Case(X::C, [] { return 68; }),
                  Default([] { return 52; }));
  };
  EXPECT_EQ(test_switch(X::A)(), Poll<int>(25));
  EXPECT_EQ(test_switch(X::B)(), Poll<int>(95));
  EXPECT_EQ(test_switch(X::C)(), Poll<int>(68));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
