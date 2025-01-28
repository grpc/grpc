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

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace grpc_core {

TEST(SeqTest, Immediate) {
  std::string execution_order;
  Poll<int> result = Seq([&execution_order] {
    absl::StrAppend(&execution_order, "1");
    return 100;
  })();
  EXPECT_EQ(result, Poll<int>(100));
  EXPECT_STREQ(execution_order.c_str(), "1");
}

TEST(SeqTest, OneThen) {
  std::string execution_order;
  auto initial = [&execution_order,
                  test_destructor_invocation1 =
                      std::make_unique<int>(1)]() -> Poll<std::string> {
    absl::StrAppend(&execution_order, "1");
    return "Hello";
  };
  auto then = [&execution_order,
               test_destructor_invocation2 =
                   std::make_unique<int>(2)](std::string initial_output) {
    absl::StrAppend(&execution_order, "2");
    return [test_destructor_invocation3 = std::make_unique<int>(3),
            &execution_order, initial_output]() -> Poll<int> {
      absl::StrAppend(&execution_order, "3");
      return initial_output.length() + 4;
    };
  };
  auto result = Seq(std::move(initial), std::move(then))();
  EXPECT_TRUE(result.ready());
  EXPECT_EQ(result.value(), 9);
  EXPECT_STREQ(execution_order.c_str(), "123");
}

TEST(SeqTest, TestPending) {
  std::string execution_order;
  bool return_pending = true;
  auto initial = [&execution_order, &return_pending,
                  test_destructor_invocation1 =
                      std::make_unique<int>(1)]() -> Poll<int> {
    absl::StrAppend(&execution_order, "1");
    if (return_pending) return Pending{};
    return 100;
  };

  auto then = [test_destructor_invocation2 = std::make_unique<int>(2),
               &execution_order](int i) {
    absl::StrAppend(&execution_order, "2");
    return [i, test_destructor_invocation3 = std::make_unique<int>(3),
            &execution_order]() -> Poll<int> {
      absl::StrAppend(&execution_order, "3");
      return i + 4;
    };
  };

  auto seq_combinator = Seq(std::move(initial), std::move(then));
  auto result = seq_combinator();
  EXPECT_EQ(result, Poll<int>(Pending{}));
  EXPECT_STREQ(execution_order.c_str(), "1");

  execution_order.clear();
  return_pending = false;
  result = seq_combinator();
  EXPECT_EQ(result, Poll<int>(104));
  EXPECT_STREQ(execution_order.c_str(), "123");
}

TEST(SeqTest, ThreeTypedPendingThens) {
  std::string execution_order;
  bool pending_a = true;
  bool pending_b = true;
  bool pending_c = true;
  bool pending_d = true;

  struct A {
    int a_ = -1;
  };
  struct B {
    int b_ = -1;
  };
  struct C {
    int c_ = -1;
  };
  struct D {
    int d_ = -1;
  };

  auto initial = [&execution_order, &pending_a]() -> Poll<A> {
    absl::StrAppend(&execution_order, "0");
    if (pending_a) {
      absl::StrAppend(&execution_order, "P");
      return Pending{};
    }
    absl::StrAppend(&execution_order, "a");
    return A{100};
  };

  auto next1 = [&execution_order, &pending_b](A a) {
    absl::StrAppend(&execution_order, "1");
    return [&execution_order, &pending_b, a]() -> Poll<B> {
      EXPECT_EQ(a.a_, 100);
      if (pending_b) {
        absl::StrAppend(&execution_order, "P");
        return Pending{};
      }
      absl::StrAppend(&execution_order, "b");
      return B{200};
    };
  };

  auto next2 = [&execution_order, &pending_c](B b) {
    absl::StrAppend(&execution_order, "2");
    return [&execution_order, &pending_c, b]() -> Poll<C> {
      EXPECT_EQ(b.b_, 200);
      if (pending_c) {
        absl::StrAppend(&execution_order, "P");
        return Pending{};
      }
      absl::StrAppend(&execution_order, "c");
      return C{300};
    };
  };

  auto next3 = [&execution_order, &pending_d](C c) {
    absl::StrAppend(&execution_order, "3");
    return [&execution_order, &pending_d, c]() -> Poll<D> {
      EXPECT_EQ(c.c_, 300);
      if (pending_d) {
        absl::StrAppend(&execution_order, "P");
        return Pending{};
      }
      absl::StrAppend(&execution_order, "d");
      return D{400};
    };
  };

  auto seq_combinator = Seq(initial, next1, next2, next3);

  auto retval = seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "0P");

  execution_order.clear();
  pending_a = false;
  retval = seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "0a1P");

  execution_order.clear();
  pending_b = false;
  retval = seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "b2P");

  execution_order.clear();
  pending_c = false;
  retval = seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "c3P");

  execution_order.clear();
  pending_d = false;
  retval = seq_combinator();
  EXPECT_TRUE(retval.ready());
  EXPECT_EQ(retval.value().d_, 400);
  EXPECT_STREQ(execution_order.c_str(), "d");
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
      Seq([test_destructor_invocation1 =
               std::make_unique<int>(1)] { return std::string("a"); },
          [test_destructor_invocation2 =
               std::make_unique<int>(2)](std::string i) {
            return [i, y = std::make_unique<int>(2)]() { return i + "b"; };
          },
          [test_destructor_invocation3 =
               std::make_unique<int>(1)](std::string i) {
            return [i, y = std::make_unique<int>(3)]() { return i + "c"; };
          },
          [test_destructor_invocation4 =
               std::make_unique<int>(1)](std::string i) {
            return [i, y = std::make_unique<int>(4)]() { return i + "d"; };
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
  LOG(INFO) << "sizeof(Big): " << sizeof(Big);  // Was 1024
  LOG(INFO) << "sizeof(p1): " << sizeof(p1);    // Was 1048
  EXPECT_GE(sizeof(p1), sizeof(Big));
  EXPECT_LT(sizeof(p1), 1.05 * sizeof(Big));  // Watchout for size bloat!
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
