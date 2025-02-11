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

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

TEST(SwitchTest, JustDefault) {
  std::string execution_order;
  auto switch_combinator = Switch(42, Default([&execution_order] {
                                    absl::StrAppend(&execution_order, "42");
                                    return 1;
                                  }));
  EXPECT_EQ(switch_combinator(), Poll<int>(1));
  EXPECT_STREQ(execution_order.c_str(), "42");
}

TEST(SwitchTest, ThreeImmediateCases) {
  std::string execution_order;
  auto test_switch = [&execution_order](int discriminator) {
    execution_order.clear();
    return Switch(discriminator, Case<1>([&execution_order] {
                    absl::StrAppend(&execution_order, "1");
                    return 100;
                  }),
                  Case<2>([&execution_order] {
                    absl::StrAppend(&execution_order, "2");
                    return 200;
                  }),
                  Case<3>([&execution_order] {
                    absl::StrAppend(&execution_order, "3");
                    return 300;
                  }),
                  Default([&execution_order] {
                    absl::StrAppend(&execution_order, "D");
                    return -1;
                  }));
  };

  EXPECT_EQ(test_switch(0)(), Poll<int>(-1));
  EXPECT_STREQ(execution_order.c_str(), "D");

  EXPECT_EQ(test_switch(1)(), Poll<int>(100));
  EXPECT_STREQ(execution_order.c_str(), "1");

  EXPECT_EQ(test_switch(2)(), Poll<int>(200));
  EXPECT_STREQ(execution_order.c_str(), "2");

  EXPECT_EQ(test_switch(3)(), Poll<int>(300));
  EXPECT_STREQ(execution_order.c_str(), "3");

  EXPECT_EQ(test_switch(4)(), Poll<int>(-1));
  EXPECT_STREQ(execution_order.c_str(), "D");
}

TEST(SwitchTest, Pending) {
  std::string execution_order;
  bool is_pending = true;
  auto test_switch = [&execution_order, &is_pending](int discriminator) {
    execution_order.clear();
    return Switch(discriminator, Case<0>([&execution_order]() -> Poll<int> {
                    absl::StrAppend(&execution_order, "0");
                    return Pending{};
                  }),
                  Case<1>([&execution_order] {
                    absl::StrAppend(&execution_order, "1");
                    return 100;
                  }),
                  Case<2>([&execution_order] {
                    absl::StrAppend(&execution_order, "2");
                    return 200;
                  }),
                  Case<3>([&execution_order, &is_pending]() -> Poll<int> {
                    absl::StrAppend(&execution_order, "3");
                    if (is_pending) return Pending{};
                    return 300;
                  }),
                  Default([&execution_order] {
                    absl::StrAppend(&execution_order, "D");
                    return -1;
                  }));
  };

  EXPECT_EQ(test_switch(0)(), Poll<int>(Pending{}));
  EXPECT_STREQ(execution_order.c_str(), "0");

  EXPECT_EQ(test_switch(1)(), Poll<int>(100));
  EXPECT_STREQ(execution_order.c_str(), "1");

  EXPECT_EQ(test_switch(2)(), Poll<int>(200));
  EXPECT_STREQ(execution_order.c_str(), "2");

  EXPECT_EQ(test_switch(3)(), Poll<int>(Pending{}));
  EXPECT_STREQ(execution_order.c_str(), "3");

  EXPECT_EQ(test_switch(4)(), Poll<int>(-1));
  EXPECT_STREQ(execution_order.c_str(), "D");

  is_pending = false;
  EXPECT_EQ(test_switch(3)(), Poll<int>(300));
  EXPECT_STREQ(execution_order.c_str(), "3");
}

TEST(SwitchTest, ThreeCasesFromEnum) {
  std::string execution_order;
  enum class X : uint8_t { A, B, C };

  auto test_switch = [&execution_order](X value) {
    execution_order.clear();
    return Switch(value, Case<X::A>([&execution_order] {
                    absl::StrAppend(&execution_order, "A");
                    return 100;
                  }),
                  Case<X::B>([&execution_order] {
                    absl::StrAppend(&execution_order, "B");
                    return 200;
                  }),
                  Case<X::C>([&execution_order] {
                    absl::StrAppend(&execution_order, "C");
                    return 300;
                  }),
                  Default([&execution_order] {
                    absl::StrAppend(&execution_order, "D");
                    return 52;
                  }));
  };

  EXPECT_EQ(test_switch(X::A)(), Poll<int>(100));
  EXPECT_STREQ(execution_order.c_str(), "A");

  EXPECT_EQ(test_switch(X::B)(), Poll<int>(200));
  EXPECT_STREQ(execution_order.c_str(), "B");

  EXPECT_EQ(test_switch(X::C)(), Poll<int>(300));
  EXPECT_STREQ(execution_order.c_str(), "C");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
