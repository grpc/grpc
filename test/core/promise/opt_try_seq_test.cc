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
#include "src/core/lib/promise/try_seq.h"

namespace grpc_core {

TEST(TrySeqTest, VoidTest0) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest1) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest2) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest3) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest4) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest5) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest6) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest7) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest8) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest9) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest10) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest11) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest12) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest13) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest14) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest15) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest16) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest17) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest18) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest19) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest20) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest21) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest22) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest23) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest24) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest25) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest26) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest27) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest28) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest29) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest30) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest31) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest32) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest33) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest34) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest35) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest36) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest37) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest38) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest39) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest40) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest41) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest42) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest43) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest44) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest45) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest46) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest47) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest48) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest49) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest50) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest51) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest52) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest53) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest54) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest55) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest56) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest57) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest58) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest59) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest60) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest61) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest62) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, VoidTest63) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}

TEST(TrySeqTest, IntTest0) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest1) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest2) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest3) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest4) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest5) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest6) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest7) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest8) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest9) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest10) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest11) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest12) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest13) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest14) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest15) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest16) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest17) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest18) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest19) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest20) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest21) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest22) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest23) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest24) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest25) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest26) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest27) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest28) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest29) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest30) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest31) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest32) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest33) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest34) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest35) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest36) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest37) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest38) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest39) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest40) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest41) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest42) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest43) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest44) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest45) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest46) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest47) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest48) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest49) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest50) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest51) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest52) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest53) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest54) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest55) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest56) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest57) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest58) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest59) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest60) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest61) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> absl::StatusOr<int> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest62) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<int> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, IntTest63) {
  std::string execution_order;
  Poll<absl::StatusOr<int>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<int>> {
        execution_order += "0";
        return absl::StatusOr<int>(0);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "1";
        return absl::StatusOr<int>(1);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "2";
        return absl::StatusOr<int>(2);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "3";
        return absl::StatusOr<int>(3);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "4";
        return absl::StatusOr<int>(4);
      },
      [&execution_order](int) -> Poll<absl::StatusOr<int>> {
        execution_order += "5";
        return absl::StatusOr<int>(5);
      })();
  EXPECT_EQ(result, Poll<absl::StatusOr<int>>(5));
  EXPECT_STREQ(execution_order.c_str(), "012345");
}

struct NamedType0 {};
struct NamedType1 {};
struct NamedType2 {};
struct NamedType3 {};
struct NamedType4 {};
struct NamedType5 {};

TEST(TrySeqTest, NamedTest0) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest1) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest2) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest3) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest4) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest5) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest6) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest7) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest8) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest9) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest10) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest11) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest12) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest13) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest14) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest15) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest16) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest17) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest18) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest19) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest20) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest21) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest22) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest23) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest24) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest25) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest26) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest27) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest28) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest29) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest30) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest31) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> absl::StatusOr<NamedType5> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest32) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest33) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest34) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest35) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest36) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest37) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest38) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest39) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest40) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest41) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest42) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest43) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest44) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest45) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest46) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest47) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> absl::StatusOr<NamedType4> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest48) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest49) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest50) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest51) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest52) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest53) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest54) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest55) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> absl::StatusOr<NamedType3> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest56) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest57) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest58) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest59) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> absl::StatusOr<NamedType2> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest60) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest61) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> absl::StatusOr<NamedType1> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest62) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> absl::StatusOr<NamedType0> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}
TEST(TrySeqTest, NamedTest63) {
  std::string execution_order;
  Poll<absl::StatusOr<NamedType5>> result = TrySeq(
      [&execution_order]() -> Poll<absl::StatusOr<NamedType0>> {
        execution_order += "0";
        return absl::StatusOr<NamedType0>(NamedType0{});
      },
      [&execution_order](NamedType0) -> Poll<absl::StatusOr<NamedType1>> {
        execution_order += "1";
        return absl::StatusOr<NamedType1>(NamedType1{});
      },
      [&execution_order](NamedType1) -> Poll<absl::StatusOr<NamedType2>> {
        execution_order += "2";
        return absl::StatusOr<NamedType2>(NamedType2{});
      },
      [&execution_order](NamedType2) -> Poll<absl::StatusOr<NamedType3>> {
        execution_order += "3";
        return absl::StatusOr<NamedType3>(NamedType3{});
      },
      [&execution_order](NamedType3) -> Poll<absl::StatusOr<NamedType4>> {
        execution_order += "4";
        return absl::StatusOr<NamedType4>(NamedType4{});
      },
      [&execution_order](NamedType4) -> Poll<absl::StatusOr<NamedType5>> {
        execution_order += "5";
        return absl::StatusOr<NamedType5>(NamedType5{});
      })();
  EXPECT_STREQ(execution_order.c_str(), "012345");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
