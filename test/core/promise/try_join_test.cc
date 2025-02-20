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

#include "src/core/lib/promise/try_join.h"

#include <functional>
#include <memory>
#include <utility>

#include "absl/utility/utility.h"
#include "gtest/gtest.h"

namespace grpc_core {

TEST(TryJoinTestBasic, TryJoinPendingFour) {
  std::string execution_order;
  bool pending_3 = true;
  bool pending_4 = true;
  bool pending_5 = true;
  bool pending_6 = true;
  auto try_join_combinator = TryJoin<absl::StatusOr>(
      [&execution_order, &pending_3]() mutable -> Poll<absl::StatusOr<int>> {
        absl::StrAppend(&execution_order, "3");
        if (pending_3) {
          absl::StrAppend(&execution_order, "P");
          return Pending{};
        }
        return 3;
      },
      [&execution_order, &pending_4]() mutable -> Poll<absl::StatusOr<double>> {
        absl::StrAppend(&execution_order, "4");
        if (pending_4) {
          absl::StrAppend(&execution_order, "P");
          return Pending{};
        }
        return 4.0;
      },
      [&execution_order,
       &pending_5]() mutable -> Poll<absl::StatusOr<std::string>> {
        absl::StrAppend(&execution_order, "5");
        if (pending_5) {
          absl::StrAppend(&execution_order, "P");
          return Pending{};
        }
        return "5";
      },
      [&execution_order, &pending_6]() mutable -> Poll<absl::StatusOr<int>> {
        absl::StrAppend(&execution_order, "6");
        if (pending_6) {
          absl::StrAppend(&execution_order, "P");
          return Pending{};
        }
        return 6;
      });

  // Asserts
  // 1. All pending promises are re-run when the TryJoin is executed.
  // 2. Resolved promises (promises which have returned a value) should not be
  //    re-run when TryJoin is executed.
  // 3. The TryJoin should return pending until all promises are resolved.
  // 4. The TryJoin should return the tuple of values when all promises are
  //    resolved.
  // 5. The order in which the promises are resolved should not matter. If the
  //    Nth promise is resolved, but the previous N promises are not resolved,
  //    they should still be re-run.
  // 6. TryJoin returns success status only if all promises return success
  //    status.

  // Execution 1 : All promises are pending. All should be run once.
  Poll<absl::StatusOr<std::tuple<int, double, std::string, int>>> retval =
      try_join_combinator();
  EXPECT_TRUE(retval.pending());
  // All promises are Pending
  EXPECT_STREQ(execution_order.c_str(), "3P4P5P6P");

  // Execution 2 : All promises should be run once. 3 gets resolved.
  execution_order.clear();
  pending_3 = false;
  retval = try_join_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "34P5P6P");

  // Execution 3 : All promises other than 3 should be run. 4 gets resolved.
  execution_order.clear();
  pending_4 = false;
  retval = try_join_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "45P6P");  // 3 should not be re-run.

  // Execution 4 : Order changed. 5 will still be pending. 6 will be resolved.
  execution_order.clear();
  pending_6 = false;
  retval = try_join_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "5P6");

  // Execution 5 : Only 5 should be run. And 5 gets resolved.
  execution_order.clear();
  pending_5 = false;
  retval = try_join_combinator();
  EXPECT_TRUE(retval.ready());  // All promises are resolved.
  EXPECT_STREQ(execution_order.c_str(), "5");

  EXPECT_TRUE(retval.value().ok());  // All promises are a success.
  EXPECT_EQ(retval.value().value(), std::tuple(3, 4.0, "5", 6));
}

TEST(TryJoinTestBasic, TryJoinPendingFailure) {
  std::string execution_order;
  bool pending_3 = true;
  bool pending_4 = true;
  bool pending_5 = true;
  auto try_join_combinator = TryJoin<absl::StatusOr>(
      [&execution_order, &pending_3]() mutable -> Poll<absl::StatusOr<int>> {
        absl::StrAppend(&execution_order, "3");
        if (pending_3) {
          absl::StrAppend(&execution_order, "P");
          return Pending{};
        }
        return 3;
      },
      [&execution_order, &pending_4]() mutable -> Poll<absl::StatusOr<double>> {
        absl::StrAppend(&execution_order, "4");
        if (pending_4) {
          absl::StrAppend(&execution_order, "P");
          return Pending{};
        }
        return absl::InternalError("Promise Errror");
      },
      [&execution_order,
       &pending_5]() mutable -> Poll<absl::StatusOr<std::string>> {
        absl::StrAppend(&execution_order, "5");
        if (pending_5) {
          absl::StrAppend(&execution_order, "P");
          return Pending{};
        }
        return "5";
      });

  // Asserts
  // 1. One failing promise in the TryJoin should cancel the execution of the
  //    remaining promises even if they are pending.
  // 2. The TryJoin should not return a tuple if any of the promises fail. It
  //    should return the correct failure status.

  // Execution 1 : All promises are pending. All should be run once.
  Poll<absl::StatusOr<std::tuple<int, double, std::string>>> retval =
      try_join_combinator();
  EXPECT_TRUE(retval.pending());
  // All promises are Pending
  EXPECT_STREQ(execution_order.c_str(), "3P4P5P");

  // Execution 2 : All promises should be run once. 3 gets resolved.
  execution_order.clear();
  pending_3 = false;
  retval = try_join_combinator();
  EXPECT_TRUE(retval.pending());
  EXPECT_STREQ(execution_order.c_str(), "34P5P");

  // Execution 3 : All promises other than 3 should be run. 4 fails.
  // 5 should not be run (even though it is pending) because 4 failed.
  execution_order.clear();
  pending_4 = false;
  retval = try_join_combinator();
  EXPECT_TRUE(retval.ready());
  EXPECT_STREQ(execution_order.c_str(), "4");

  EXPECT_EQ(retval.value().status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(retval.value().status().message(), "Promise Errror");
}

struct AbslStatusTraits {
  template <typename... Promises>
  static auto TryJoinImpl(Promises... promises) {
    return TryJoin<absl::StatusOr>(std::move(promises)...);
  }

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

  template <typename... T>
  static Poll<absl::StatusOr<std::tuple<T...>>> ok(T... x) {
    return absl::StatusOr<std::tuple<T...>>(absl::in_place, x...);
  }

  template <typename... T>
  static Poll<absl::StatusOr<std::tuple<T...>>> fail() {
    return absl::StatusOr<std::tuple<T...>>();
  }

  template <typename T>
  static Promise<T> pending() {
    return []() -> Poll<absl::StatusOr<T>> { return Pending(); };
  }
};

struct ValueOrFailureTraits {
  template <typename... Promises>
  static auto TryJoinImpl(Promises... promises) {
    return TryJoin<ValueOrFailure>(std::move(promises)...);
  }

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

  template <typename... T>
  static Poll<ValueOrFailure<std::tuple<T...>>> ok(T... x) {
    return ValueOrFailure<std::tuple<T...>>(std::tuple<T...>(x...));
  }

  template <typename... T>
  static Poll<ValueOrFailure<std::tuple<T...>>> fail() {
    return Failure{};
  }

  template <typename T>
  static Promise<T> pending() {
    return []() -> Poll<ValueOrFailure<T>> { return Pending(); };
  }
};

template <typename T>
class TryJoinTest : public ::testing::Test {};

using Traits = ::testing::Types<AbslStatusTraits, ValueOrFailureTraits>;
TYPED_TEST_SUITE(TryJoinTest, Traits);

TYPED_TEST(TryJoinTest, Join1) {
  EXPECT_EQ(TypeParam::TryJoinImpl(TypeParam::instant_ok(1))(),
            TypeParam::ok(1));
}

TYPED_TEST(TryJoinTest, Join1Fail) {
  EXPECT_EQ(TypeParam::TryJoinImpl(TypeParam::template instant_fail<int>())(),
            TypeParam::template fail<int>());
}

TYPED_TEST(TryJoinTest, Join2Success) {
  EXPECT_EQ(TypeParam::TryJoinImpl(TypeParam::instant_ok(1),
                                   TypeParam::instant_ok(2))(),
            TypeParam::ok(1, 2));
}

TYPED_TEST(TryJoinTest, Join2Fail1) {
  EXPECT_EQ(TypeParam::TryJoinImpl(TypeParam::instant_ok(1),
                                   TypeParam::template instant_fail<int>())(),
            (TypeParam::template fail<int, int>()));
}

TYPED_TEST(TryJoinTest, Join2Fail2) {
  EXPECT_EQ(TypeParam::TryJoinImpl(TypeParam::template instant_fail<int>(),
                                   TypeParam::instant_ok(2))(),
            (TypeParam::template fail<int, int>()));
}

TYPED_TEST(TryJoinTest, Join2Fail1P) {
  EXPECT_EQ(TypeParam::TryJoinImpl(TypeParam::template pending<int>(),
                                   TypeParam::template instant_fail<int>())(),
            (TypeParam::template fail<int, int>()));
}

TYPED_TEST(TryJoinTest, Join2Fail2P) {
  EXPECT_EQ(TypeParam::TryJoinImpl(TypeParam::template instant_fail<int>(),
                                   TypeParam::template pending<int>())(),
            (TypeParam::template fail<int, int>()));
}

TYPED_TEST(TryJoinTest, JoinStatus) {
  EXPECT_EQ(TypeParam::TryJoinImpl(TypeParam::instant_ok_status(),
                                   TypeParam::instant_ok_status())(),
            TypeParam::ok(Empty{}, Empty{}));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
