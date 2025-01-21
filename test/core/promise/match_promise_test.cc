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

#include "src/core/lib/promise/match_promise.h"

#include <memory>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "src/core/lib/promise/promise.h"
#include "test/core/promise/poll_matcher.h"

namespace grpc_core {

struct MatchInt {
  int ival = -1;
};

struct MatchFloat {
  float fval = -1.0f;
};

using V = std::variant<MatchInt, MatchFloat, std::string>;

TEST(MatchPromiseTest, ThreeTypedImmediate) {
  std::string execution_order;

  auto make_promise = [&execution_order](V variant) -> Promise<std::string> {
    return MatchPromise(
        std::move(variant),
        [&execution_order](MatchFloat match_float) -> Poll<std::string> {
          absl::StrAppend(&execution_order, "F");
          return absl::StrCat(match_float.fval);
        },
        [&execution_order](MatchInt match_int) -> Poll<std::string> {
          absl::StrAppend(&execution_order, "I");
          return absl::StrCat(match_int.ival);
        },
        [&execution_order](std::string match_str) {
          absl::StrAppend(&execution_order, "S");
          return match_str;
        });
  };

  V float_variant = MatchFloat{3.0f};
  auto promise = make_promise(float_variant);
  EXPECT_THAT(promise(), IsReady("3"));
  EXPECT_STREQ(execution_order.c_str(), "F");

  execution_order.clear();
  V int_variant = MatchInt{42};
  promise = make_promise(int_variant);
  EXPECT_THAT(promise(), IsReady("42"));
  EXPECT_STREQ(execution_order.c_str(), "I");

  execution_order.clear();
  V string_variant = "hello";
  promise = make_promise(string_variant);
  EXPECT_THAT(promise(), IsReady("hello"));
  EXPECT_STREQ(execution_order.c_str(), "S");
}

TEST(MatchPromiseTest, ThreeTypedPending) {
  std::string execution_order;

  auto make_promise = [&execution_order](V variant) -> Promise<std::string> {
    return MatchPromise(
        std::move(variant),
        [&execution_order](MatchFloat match_float) mutable {
          return [n = 3, match_float,
                  &execution_order]() mutable -> Poll<std::string> {
            absl::StrAppend(&execution_order, "F");
            --n;
            if (n > 0) {
              absl::StrAppend(&execution_order, "P");
              return Pending{};
            }
            return absl::StrCat(match_float.fval);
          };
        },
        [&execution_order](MatchInt match_int) {
          return [&execution_order]() mutable -> Poll<std::string> {
            absl::StrAppend(&execution_order, "I");
            return Pending{};
          };
        },
        [&execution_order](std::string match_str) {
          absl::StrAppend(&execution_order, "S");
          return match_str;
        });
  };

  V float_variant = MatchFloat{3.0f};
  auto promise = make_promise(float_variant);
  EXPECT_THAT(promise(), IsPending());
  EXPECT_THAT(promise(), IsPending());
  EXPECT_THAT(promise(), IsReady("3"));
  EXPECT_STREQ(execution_order.c_str(), "FPFPF");

  execution_order.clear();
  V int_variant = MatchInt{42};
  promise = make_promise(int_variant);
  for (int i = 0; i < 60; i++) {
    EXPECT_THAT(promise(), IsPending());
  }
  EXPECT_STREQ(execution_order.c_str(),
               "IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII");

  execution_order.clear();
  V string_variant = "hello";
  promise = make_promise(string_variant);
  EXPECT_THAT(promise(), IsReady("hello"));
  EXPECT_STREQ(execution_order.c_str(), "S");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
