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
