// Copyright 2025 gRPC authors.
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

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/seq.h"

namespace grpc_core {

TEST(SeqTest, VoidTest0) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest1) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest2) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest3) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest4) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest5) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest6) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest7) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest8) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest9) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest10) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest11) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest12) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest13) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest14) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest15) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest16) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest17) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest18) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest19) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest20) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest21) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest22) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest23) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest24) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest25) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest26) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest27) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest28) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest29) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest30) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest31) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest32) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest33) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest34) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest35) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest36) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest37) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest38) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest39) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest40) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest41) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest42) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest43) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest44) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest45) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest46) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest47) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest48) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest49) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest50) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest51) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest52) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest53) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest54) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest55) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest56) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest57) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest58) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest59) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest60) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest61) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest62) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, VoidTest63) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order]() -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}

TEST(SeqTest, IntTest0) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest1) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest2) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest3) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest4) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest5) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest6) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest7) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest8) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest9) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest10) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest11) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest12) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest13) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest14) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest15) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest16) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest17) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest18) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest19) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest20) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest21) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest22) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest23) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest24) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest25) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest26) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest27) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest28) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest29) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest30) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest31) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> int {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest32) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest33) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest34) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest35) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest36) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest37) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest38) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest39) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest40) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest41) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest42) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest43) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest44) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest45) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest46) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest47) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> int {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest48) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest49) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest50) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest51) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest52) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest53) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest54) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest55) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> int {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest56) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest57) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest58) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest59) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> int {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest60) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest61) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> int {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest62) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> int {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, IntTest63) {
  std::string execution_order;
  Poll<int> result = Seq(
      [&execution_order]() -> Poll<int> {
        execution_order += "0";
        return 0;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "1";
        return 1;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "2";
        return 2;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "3";
        return 3;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "4";
        return 4;
      },
      [&execution_order](int) -> Poll<int> {
        execution_order += "5";
        return 5;
      })();
  EXPECT_EQ(result, Poll<int>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}

struct NamedType0 {};
struct NamedType1 {};
struct NamedType2 {};
struct NamedType3 {};
struct NamedType4 {};
struct NamedType5 {};

TEST(SeqTest, NamedTest0) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest1) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest2) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest3) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest4) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest5) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest6) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest7) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest8) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest9) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest10) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest11) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest12) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest13) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest14) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest15) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest16) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest17) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest18) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest19) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest20) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest21) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest22) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest23) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest24) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest25) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest26) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest27) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest28) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest29) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest30) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest31) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> NamedType5 {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest32) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest33) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest34) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest35) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest36) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest37) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest38) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest39) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest40) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest41) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest42) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest43) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest44) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest45) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest46) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest47) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> NamedType4 {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest48) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest49) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest50) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest51) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest52) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest53) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest54) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest55) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> NamedType3 {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest56) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest57) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest58) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest59) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> NamedType2 {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest60) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest61) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> NamedType1 {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest62) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> NamedType0 {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(SeqTest, NamedTest63) {
  std::string execution_order;
  Poll<NamedType5> result = Seq(
      [&execution_order]() -> Poll<NamedType0> {
        execution_order += "0";
        return NamedType0{};
      },
      [&execution_order](NamedType0) -> Poll<NamedType1> {
        execution_order += "1";
        return NamedType1{};
      },
      [&execution_order](NamedType1) -> Poll<NamedType2> {
        execution_order += "2";
        return NamedType2{};
      },
      [&execution_order](NamedType2) -> Poll<NamedType3> {
        execution_order += "3";
        return NamedType3{};
      },
      [&execution_order](NamedType3) -> Poll<NamedType4> {
        execution_order += "4";
        return NamedType4{};
      },
      [&execution_order](NamedType4) -> Poll<NamedType5> {
        execution_order += "5";
        return NamedType5{};
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
