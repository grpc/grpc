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

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace grpc_core {

TEST(SeqTest, Immediate) {
  EXPECT_EQ(Seq([] { return 3; })(), 3);
}

TEST(SeqTest, OneThen) {
  auto initial = [a = std::make_unique<int>(0)] { return 3; };
  auto then = [a = std::make_unique<int>(1)](int i) {
    return [i, b = std::make_unique<int>(2)]() { return i + 4; };
  };
  EXPECT_EQ(Seq(std::move(initial), std::move(then))(), Poll<int>(7));
}

TEST(SeqTest, OneThenIncomplete) {
  auto initial = [a = std::make_unique<int>(0)]() -> Poll<int> {
    return Pending{};
  };
  auto then = [a = std::make_unique<int>(1)](int i) {
    return [i, b = std::make_unique<int>(2)]() { return i + 4; };
  };
  EXPECT_EQ(Seq(std::move(initial), std::move(then))(), Poll<int>(Pending{}));
}

TEST(SeqTest, TwoTypedThens) {
  struct A {};
  struct B {};
  struct C {};
  auto initial = [] { return A{}; };
  auto next1 = [](A) { return []() { return B{}; }; };
  auto next2 = [](B) { return []() { return C{}; }; };
  EXPECT_FALSE(Seq(initial, next1, next2)().pending());
}

// This does not compile, but is useful for testing error messages generated
// TEST(SeqTest, MisTypedThen) {
// struct A {};
// struct B {};
// auto initial = [] { return A{}; };
// auto next = [](B) { return []() { return B{}; }; };
// Seq(initial, next)().take();
//}
//

TEST(SeqTest, TwoThens) {
  auto initial = [] { return std::string("a"); };
  auto next1 = [](std::string i) { return [i]() { return i + "b"; }; };
  auto next2 = [](std::string i) { return [i]() { return i + "c"; }; };
  EXPECT_EQ(Seq(initial, next1, next2)(), Poll<std::string>("abc"));
}

TEST(SeqTest, ThreeThens) {
  EXPECT_EQ(
      Seq([x = std::make_unique<int>(1)] { return std::string("a"); },
          [x = std::make_unique<int>(1)](std::string i) {
            return [i, y = std::make_unique<int>(2)]() { return i + "b"; };
          },
          [x = std::make_unique<int>(1)](std::string i) {
            return [i, y = std::make_unique<int>(2)]() { return i + "c"; };
          },
          [x = std::make_unique<int>(1)](std::string i) {
            return [i, y = std::make_unique<int>(2)]() { return i + "d"; };
          })(),
      Poll<std::string>("abcd"));
}

struct Big {
  int x[256];
  void YesItIsUnused() const {}
};

TEST(SeqTest, SaneSizes) {
  auto x = Big();
  auto p1 = Seq(
      [x] {
        x.YesItIsUnused();
        return 1;
      },
      [](int) {
        auto y = Big();
        return [y]() {
          y.YesItIsUnused();
          return 2;
        };
      });
  EXPECT_GE(sizeof(p1), sizeof(Big));
  EXPECT_LT(sizeof(p1), 2 * sizeof(Big));
}

TEST(SeqIterTest, Accumulate) {
  std::vector<int> v{1, 2, 3, 4, 5};
  EXPECT_EQ(SeqIter(v.begin(), v.end(), 0,
                    [](int cur, int next) {
                      return [cur, next]() { return cur + next; };
                    })(),
            Poll<int>(15));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
