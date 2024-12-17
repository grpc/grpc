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

#include "src/core/lib/promise/loop.h"

#include <memory>
#include <utility>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/seq.h"

namespace grpc_core {

TEST(LoopTest, CountToFive) {
  std::string execution_order = "";
  int i = 0;
  auto retval = Loop([&execution_order, &i]() -> LoopCtl<int> {
    absl::StrAppend(&execution_order, i);
    i++;
    if (i < 5) return Continue();
    return i;
  })();
  EXPECT_TRUE(retval.ready());
  EXPECT_EQ(retval.value(), 5);
  EXPECT_EQ(i, 5);
  EXPECT_STREQ(execution_order.c_str(), "01234");
}

TEST(LoopTest, FactoryCountToFive) {
  int i = 0;
  Loop([&i]() {
    return [&i]() -> LoopCtl<int> {
      i++;
      if (i < 5) return Continue();
      return i;
    };
  })();
  EXPECT_EQ(i, 5);
}

TEST(LoopTest, LoopOfSeq) {
  std::string execution_order = "";
  auto retval = Loop(Seq(
      [&execution_order]() mutable -> Poll<int> {
        absl::StrAppend(&execution_order, "a");
        return 42;
      },
      [&execution_order](int i) mutable -> LoopCtl<int> {
        absl::StrAppend(&execution_order, i);
        return i;
      }))();
  EXPECT_TRUE(retval.ready());
  EXPECT_EQ(retval, Poll<int>(42));
  EXPECT_STREQ(execution_order.c_str(), "a42");
}

TEST(LoopTest, LoopOfSeq1) {
  std::string execution_order = "";
  auto retval = Loop(Seq(
      [&execution_order]() mutable -> Poll<int> {
        absl::StrAppend(&execution_order, "a");
        return execution_order.length();
      },
      [&execution_order](int i) mutable -> LoopCtl<int> {
        absl::StrAppend(&execution_order, i);
        if (i < 9) return Continue();
        return i;
      }))();
  EXPECT_TRUE(retval.ready());
  EXPECT_EQ(retval, Poll<int>(9));
  EXPECT_STREQ(execution_order.c_str(), "a1a3a5a7a9");
}

TEST(LoopTest, CanAccessFactoryLambdaVariables) {
  int i = 0;
  auto x = Loop([p = &i]() {
    return [q = &p]() -> Poll<LoopCtl<int>> {
      ++**q;
      return Pending{};
    };
  });
  auto y = std::move(x);
  auto z = std::move(y);
  z();
  EXPECT_EQ(i, 1);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
