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

#include "src/core/lib/promise/map.h"

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/promise.h"
#include "test/core/promise/poll_matcher.h"

namespace grpc_core {

TEST(MapTest, Works) {
  Promise<int> x = Map([]() { return 42; }, [](int i) { return i / 2; });
  EXPECT_THAT(x(), IsReady(21));
}

TEST(MapTest, JustElem) {
  std::tuple<int, double> t(1, 3.2);
  EXPECT_EQ(JustElem<1>()(t), 3.2);
  EXPECT_EQ(JustElem<0>()(t), 1);
}

TEST(CheckDelayedTest, SeesImmediate) {
  auto x = CheckDelayed([]() { return 42; });
  EXPECT_THAT(x(), IsReady(std::tuple(42, false)));
}

TEST(CheckDelayedTest, SeesDelayed) {
  auto x = CheckDelayed([n = 1]() mutable -> Poll<int> {
    if (n == 0) return 42;
    --n;
    return Pending{};
  });
  EXPECT_THAT(x(), IsPending());
  EXPECT_THAT(x(), IsReady(std::tuple(42, true)));
}

TEST(MapError, DoesntMapOk) {
  auto fail_on_call = [](const absl::Status&) {
    LOG(FATAL) << "should never be called";
    return absl::InternalError("unreachable");
  };
  promise_detail::MapError<decltype(fail_on_call)> map_on_error(
      std::move(fail_on_call));
  EXPECT_EQ(absl::OkStatus(), map_on_error(absl::OkStatus()));
}

TEST(MapError, CanMapError) {
  auto map_call = [](const absl::Status& status) {
    EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
    EXPECT_EQ(status.message(), "hello");
    return absl::UnavailableError("world");
  };
  promise_detail::MapError<decltype(map_call)> map_on_error(
      std::move(map_call));
  auto mapped = map_on_error(absl::InternalError("hello"));
  EXPECT_EQ(mapped.code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(mapped.message(), "world");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
