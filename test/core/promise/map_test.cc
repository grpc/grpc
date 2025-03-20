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

TEST(MapTest, TwoTyped) {
  auto map =
      AssertResultType<std::string>(Map([]() { return absl::OkStatus(); },
                                        [](absl::Status s) -> std::string {
                                          if (s.ok()) {
                                            return "OK";
                                          } else {
                                            return "ERROR";
                                          }
                                        }));
  auto result = map();
  EXPECT_THAT(result, IsReady());
  EXPECT_EQ(result.value(), std::string("OK"));
}

TEST(MapTest, ReturnsPending) {
  int once = 1;
  std::string execution_order;

  auto map = Map(
      [&once, &execution_order]() -> Poll<int> {
        if (once == 0) {
          execution_order.push_back('2');
          return 42;
        }
        once--;
        execution_order.push_back('1');
        return Pending{};
      },
      [&execution_order](int i) {
        execution_order.push_back('3');
        return i / 2;
      });

  auto first_result = map();
  EXPECT_THAT(first_result, IsPending());

  auto second_result = map();
  EXPECT_THAT(second_result, IsReady());
  EXPECT_EQ(second_result, Poll<int>(21));
  EXPECT_STREQ(execution_order.c_str(), "123");
}

TEST(MapTest, ReturnsVoid) {
  int final_result = 0;
  auto map =
      Map([]() { return 42; }, [&final_result](int i) { final_result = i; });

  auto result = map();
  EXPECT_THAT(result, IsReady());
  EXPECT_EQ(result.value(), Empty{});
  EXPECT_EQ(final_result, 42);
}

TEST(MapTest, NestedMaps) {
  auto map1 = Map([]() { return 42; }, [](int i) { return i / 2; });
  Promise<int> promise = Map(std::move(map1), [](int i) { return i + 10; });

  auto result = promise();
  EXPECT_THAT(result, IsReady());
  EXPECT_EQ(result, Poll<int>(31));
}

TEST(MapTest, NestedMapsWithDifferentTypes) {
  int i = 30;
  std::string execution_order{};
  auto map1 = AssertResultType<absl::Status>(Map(
      [i, &execution_order]() {
        execution_order.push_back('1');
        return i;
      },
      [&execution_order](int i) {
        execution_order.push_back('2');
        EXPECT_EQ(i, 30);
        return absl::OkStatus();
      }));
  auto map2 = AssertResultType<std::string>(
      Map(std::move(map1), [&execution_order](absl::Status s) -> std::string {
        execution_order.push_back('3');
        if (s.ok()) {
          return "OK";
        } else {
          return "ERROR";
        }
      }));

  auto result = map2();
  EXPECT_THAT(result, IsReady());
  EXPECT_EQ(result, Poll<std::string>("OK"));
  EXPECT_STREQ(execution_order.c_str(), "123");
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

TEST(CheckDelayedTest, SeesImmediateWithMap) {
  auto map = Map([]() { return absl::OkStatus(); },
                 [](absl::Status s) {
                   if (s.ok()) {
                     return "OK";
                   } else {
                     return "ERROR";
                   }
                 });
  auto promise = AssertResultType<std::tuple<const char*, bool>>(
      CheckDelayed(std::move(map)));

  auto result = promise();
  EXPECT_THAT(result, IsReady());
  EXPECT_STREQ(JustElem<0>()(result.value()), "OK");
  EXPECT_EQ(JustElem<1>()(result.value()), false);
}

TEST(CheckDelayedTest, SeesDelayedWithMap) {
  int once = 1;
  std::string execution_order;

  auto map = Map(
      [&once, &execution_order]() -> Poll<int> {
        if (once == 0) {
          execution_order.push_back('2');
          return 42;
        }
        once--;
        execution_order.push_back('1');
        return Pending{};
      },
      [&execution_order](int i) {
        execution_order.push_back('3');
        return i / 2;
      });
  auto promise =
      AssertResultType<std::tuple<int, bool>>(CheckDelayed(std::move(map)));

  EXPECT_THAT(promise(), IsPending());
  EXPECT_THAT(promise(), IsReady(std::tuple(21, true)));
  EXPECT_STREQ(execution_order.c_str(), "123");
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
