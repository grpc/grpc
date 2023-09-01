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
#include <tuple>

#include "absl/utility/utility.h"
#include "gtest/gtest.h"

namespace grpc_core {

template <typename T>
using P = std::function<Poll<absl::StatusOr<T>>()>;

template <typename T>
P<T> instant_ok(T x) {
  return [x] { return absl::StatusOr<T>(x); };
}

template <typename T>
P<T> instant_fail() {
  return [] { return absl::StatusOr<T>(); };
}

template <typename... T>
Poll<absl::StatusOr<std::tuple<T...>>> ok(T... x) {
  return absl::StatusOr<std::tuple<T...>>(absl::in_place, x...);
}

template <typename... T>
Poll<absl::StatusOr<std::tuple<T...>>> fail() {
  return absl::StatusOr<std::tuple<T...>>();
}

template <typename T>
P<T> pending() {
  return []() -> Poll<absl::StatusOr<T>> { return Pending(); };
}

TEST(TryJoinTest, Join1) { EXPECT_EQ(TryJoin(instant_ok(1))(), ok(1)); }

TEST(TryJoinTest, Join1Fail) {
  EXPECT_EQ(TryJoin(instant_fail<int>())(), fail<int>());
}

TEST(TryJoinTest, Join2Success) {
  EXPECT_EQ(TryJoin(instant_ok(1), instant_ok(2))(), ok(1, 2));
}

TEST(TryJoinTest, Join2Fail1) {
  EXPECT_EQ(TryJoin(instant_ok(1), instant_fail<int>())(), (fail<int, int>()));
}

TEST(TryJoinTest, Join2Fail2) {
  EXPECT_EQ(TryJoin(instant_fail<int>(), instant_ok(2))(), (fail<int, int>()));
}

TEST(TryJoinTest, Join2Fail1P) {
  EXPECT_EQ(TryJoin(pending<int>(), instant_fail<int>())(), (fail<int, int>()));
}

TEST(TryJoinTest, Join2Fail2P) {
  EXPECT_EQ(TryJoin(instant_fail<int>(), pending<int>())(), (fail<int, int>()));
}

TEST(TryJoinTest, JoinStatus) {
  EXPECT_EQ(TryJoin([]() { return absl::OkStatus(); },
                    []() { return absl::OkStatus(); })(),
            ok(Empty{}, Empty{}));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
