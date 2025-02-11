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

#include "src/core/lib/promise/try_seq.h"

#include <stdlib.h>

#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "test/core/promise/poll_matcher.h"

namespace grpc_core {

TEST(TrySeqTestBasic, ThreeTypedPendingThens) {
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
    return [&execution_order, &pending_c, b]() -> Poll<absl::StatusOr<C>> {
      EXPECT_EQ(b.b_, 200);
      if (pending_c) {
        absl::StrAppend(&execution_order, "P");
        return Pending{};
      }
      absl::StrAppend(&execution_order, "Fail");
      return absl::StatusOr<C>();
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

  auto try_seq_combinator = TrySeq(initial, next1, next2, next3);

  auto retval = try_seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "0P");

  execution_order.clear();
  pending_a = false;
  retval = try_seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "0a1P");

  execution_order.clear();
  pending_b = false;
  retval = try_seq_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "b2P");

  execution_order.clear();
  pending_c = false;
  retval = try_seq_combinator();
  EXPECT_TRUE(retval.ready());
  EXPECT_STREQ(execution_order.c_str(), "Fail");
}

struct AbslStatusTraits {
  template <typename T>
  using Promise = std::function<Poll<absl::StatusOr<T>>()>;

  template <typename T>
  static Promise<T> instant_ok(T x) {
    return [x] { return absl::StatusOr<T>(x); };
  }

  static auto instant_ok_status() {
    return [] { return absl::OkStatus(); };
  }

  template <typename T>
  static Promise<T> instant_fail() {
    return [] { return absl::StatusOr<T>(); };
  }

  template <typename T>
  static Poll<absl::StatusOr<T>> instant_crash() {
    abort();
  }

  template <typename T>
  static Poll<absl::StatusOr<T>> ok(T x) {
    return absl::StatusOr<T>(x);
  }

  static Poll<absl::Status> ok_status() { return absl::OkStatus(); }

  template <typename T>
  static Poll<absl::StatusOr<T>> fail() {
    return absl::StatusOr<T>();
  }
};

struct ValueOrFailureTraits {
  template <typename T>
  using Promise = std::function<Poll<ValueOrFailure<T>>()>;

  template <typename T>
  static Promise<T> instant_ok(T x) {
    return [x] { return ValueOrFailure<T>(x); };
  }

  static auto instant_ok_status() {
    return [] { return StatusFlag(true); };
  }

  template <typename T>
  static Promise<T> instant_fail() {
    return [] { return Failure{}; };
  }

  template <typename T>
  static Poll<ValueOrFailure<T>> instant_crash() {
    abort();
  }

  template <typename T>
  static Poll<ValueOrFailure<T>> ok(T x) {
    return ValueOrFailure<T>(x);
  }

  static Poll<StatusFlag> ok_status() { return Success{}; }

  template <typename T>
  static Poll<ValueOrFailure<T>> fail() {
    return Failure{};
  }
};

template <typename T>
class TrySeqTest : public ::testing::Test {};

using Traits = ::testing::Types<AbslStatusTraits, ValueOrFailureTraits>;
TYPED_TEST_SUITE(TrySeqTest, Traits);

TYPED_TEST(TrySeqTest, SucceedAndThen) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(TypeParam::instant_ok(1),
                   [&execution_order](int i) {
                     absl::StrAppend(&execution_order, "1");
                     return TypeParam::instant_ok(i + 1);
                   })(),
            TypeParam::ok(2));
  EXPECT_STREQ(execution_order.c_str(), "1");
}

TYPED_TEST(TrySeqTest, SucceedDirectlyAndThenDirectly) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(
                [&execution_order] {
                  absl::StrAppend(&execution_order, "1");
                  return 1;
                },
                [&execution_order](int i) {
                  absl::StrAppend(&execution_order, "2");
                  return [i, &execution_order]() {
                    absl::StrAppend(&execution_order, "3");
                    return i + 1;
                  };
                })(),
            Poll<absl::StatusOr<int>>(2));
  EXPECT_STREQ(execution_order.c_str(), "123");
}

TYPED_TEST(TrySeqTest, SucceedAndThenChangeType) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(TypeParam::instant_ok(42),
                   [&execution_order](int i) {
                     absl::StrAppend(&execution_order, "1");
                     return TypeParam::instant_ok(std::to_string(i));
                   })(),
            TypeParam::ok(std::string("42")));
  EXPECT_STREQ(execution_order.c_str(), "1");
}

TYPED_TEST(TrySeqTest, FailAndThen) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(TypeParam::template instant_fail<int>(),
                   [&execution_order](int) {
                     absl::StrAppend(&execution_order, "1");
                     return TypeParam::template instant_crash<double>();
                   })(),
            TypeParam::template fail<double>());
  EXPECT_STREQ(execution_order.c_str(), "");
}

TYPED_TEST(TrySeqTest, RawSucceedAndThen) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(TypeParam::instant_ok_status(),
                   [&execution_order] {
                     absl::StrAppend(&execution_order, "1");
                     return TypeParam::instant_ok_status();
                   })(),
            TypeParam::ok_status());
  EXPECT_STREQ(execution_order.c_str(), "1");
}

TYPED_TEST(TrySeqTest, RawFailAndThen) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(
                [&execution_order] {
                  absl::StrAppend(&execution_order, "1");
                  return absl::CancelledError();
                },
                [&execution_order]() {
                  absl::StrAppend(&execution_order, "2");
                  return [&execution_order]() -> Poll<absl::Status> {
                    absl::StrAppend(&execution_order, "3");
                    abort();
                  };
                })(),
            Poll<absl::Status>(absl::CancelledError()));
  EXPECT_STREQ(execution_order.c_str(), "1");
}

TYPED_TEST(TrySeqTest, RawSucceedAndThenValue) {
  std::string execution_order;
  EXPECT_EQ(TrySeq(
                [&execution_order] {
                  absl::StrAppend(&execution_order, "1");
                  return absl::OkStatus();
                },
                [&execution_order] {
                  absl::StrAppend(&execution_order, "2");
                  return [&execution_order]() {
                    absl::StrAppend(&execution_order, "3");
                    return absl::StatusOr<int>(42);
                  };
                })(),
            Poll<absl::StatusOr<int>>(absl::StatusOr<int>(42)));
  EXPECT_STREQ(execution_order.c_str(), "123");
}

TEST(TrySeqIterTest, Ok) {
  std::vector<int> v{1, 2, 3, 4, 5};
  EXPECT_EQ(TrySeqIter(v.begin(), v.end(), 0,
                       [](int elem, int accum) {
                         return [elem, accum]() -> absl::StatusOr<int> {
                           return elem + accum;
                         };
                       })(),
            Poll<absl::StatusOr<int>>(15));
}

TEST(TrySeqIterTest, ErrorAt3) {
  std::vector<int> v{1, 2, 3, 4, 5};
  EXPECT_EQ(TrySeqIter(v.begin(), v.end(), 0,
                       [](int elem, int accum) {
                         return [elem, accum]() -> absl::StatusOr<int> {
                           if (elem < 3) {
                             return elem + accum;
                           }
                           if (elem == 3) {
                             return absl::CancelledError();
                           }
                           abort();  // unreachable
                         };
                       })(),
            Poll<absl::StatusOr<int>>(absl::CancelledError()));
}

TEST(TrySeqContainer, Ok) {
  std::vector<std::unique_ptr<int>> v;
  v.emplace_back(std::make_unique<int>(1));
  v.emplace_back(std::make_unique<int>(2));
  v.emplace_back(std::make_unique<int>(3));
  int expect = 1;
  auto p = TrySeqContainer(std::move(v), Empty{},
                           [&expect](const std::unique_ptr<int>& i, Empty) {
                             EXPECT_EQ(*i, expect);
                             ++expect;
                           });
  EXPECT_THAT(p(), IsReady());
  EXPECT_EQ(expect, 4);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
