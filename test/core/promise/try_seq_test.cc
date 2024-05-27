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

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace grpc_core {

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

  template <typename T>
  static Promise<T> pending() {
    return []() -> Poll<absl::StatusOr<T>> { return Pending(); };
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

  template <typename T>
  static Promise<T> pending() {
    return []() -> Poll<ValueOrFailure<T>> { return Pending(); };
  }
};

template <typename T>
class TrySeqTest : public ::testing::Test {};

using Traits = ::testing::Types<AbslStatusTraits, ValueOrFailureTraits>;
TYPED_TEST_SUITE(TrySeqTest, Traits);

TYPED_TEST(TrySeqTest, SucceedAndThen) {
  EXPECT_EQ(TrySeq(TypeParam::instant_ok(1),
                   [](int i) { return TypeParam::instant_ok(i + 1); })(),
            TypeParam::ok(2));
}

TYPED_TEST(TrySeqTest, SucceedDirectlyAndThenDirectly) {
  EXPECT_EQ(
      TrySeq([] { return 1; }, [](int i) { return [i]() { return i + 1; }; })(),
      Poll<absl::StatusOr<int>>(2));
}

TYPED_TEST(TrySeqTest, SucceedAndThenChangeType) {
  EXPECT_EQ(
      TrySeq(TypeParam::instant_ok(42),
             [](int i) { return TypeParam::instant_ok(std::to_string(i)); })(),
      TypeParam::ok(std::string("42")));
}

TYPED_TEST(TrySeqTest, FailAndThen) {
  EXPECT_EQ(
      TrySeq(TypeParam::template instant_fail<int>(),
             [](int) { return TypeParam::template instant_crash<double>(); })(),
      TypeParam::template fail<double>());
}

TYPED_TEST(TrySeqTest, RawSucceedAndThen) {
  EXPECT_EQ(TrySeq(TypeParam::instant_ok_status(),
                   [] { return TypeParam::instant_ok_status(); })(),
            TypeParam::ok_status());
}

TYPED_TEST(TrySeqTest, RawFailAndThen) {
  EXPECT_EQ(TrySeq([] { return absl::CancelledError(); },
                   []() { return []() -> Poll<absl::Status> { abort(); }; })(),
            Poll<absl::Status>(absl::CancelledError()));
}

TYPED_TEST(TrySeqTest, RawSucceedAndThenValue) {
  EXPECT_EQ(TrySeq([] { return absl::OkStatus(); },
                   [] { return []() { return absl::StatusOr<int>(42); }; })(),
            Poll<absl::StatusOr<int>>(absl::StatusOr<int>(42)));
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

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
