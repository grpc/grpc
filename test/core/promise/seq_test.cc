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

#include "src/core/lib/promise/seq.h"
#include <gtest/gtest.h>

namespace grpc_core {

TEST(PromiseTest, Immediate) {
  EXPECT_EQ(Seq([] { return ready(3); })().take(), 3);
}

TEST(PromiseTest, OneThen) {
  EXPECT_EQ(Seq([] { return ready(3); },
                [](int i) { return [i]() { return ready(i + 4); }; })()
                .take(),
            7);
}

TEST(PromiseTest, TwoThens) {
  EXPECT_EQ(
      Seq([] { return ready(std::string("a")); },
          [](std::string i) { return [i]() { return ready(i + "b"); }; },
          [](std::string i) { return [i]() { return ready(i + "c"); }; })()
          .take(),
      "abc");
}

TEST(PromiseTest, ThreeThens) {
  EXPECT_EQ(
      Seq([] { return ready(std::string("a")); },
          [](std::string i) { return [i]() { return ready(i + "b"); }; },
          [](std::string i) { return [i]() { return ready(i + "c"); }; },
          [](std::string i) { return [i]() { return ready(i + "d"); }; })()
          .take(),
      "abcd");
}

struct Big {
  int x[256];
  void YesItIsUnused() const {}
};

TEST(PromiseTest, SaneSizes) {
  auto x = Big();
  auto p1 = Seq(
      [x] {
        x.YesItIsUnused();
        return ready(1);
      },
      [](int i) {
        auto y = Big();
        return [y]() {
          y.YesItIsUnused();
          return ready(2);
        };
      });
  EXPECT_GE(sizeof(p1), sizeof(Big));
  EXPECT_LT(sizeof(p1), 2 * sizeof(Big));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
