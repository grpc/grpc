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

TEST(MatchPromiseTest, Works) {
  struct Int {
    int x;
  };
  struct Float {
    float x;
  };
  using V = absl::variant<Int, Float, std::string>;
  auto make_promise = [](V v) -> Promise<std::string> {
    return MatchPromise(
        std::move(v),
        [](Float x) mutable {
          return [n = 3, x]() mutable -> Poll<std::string> {
            --n;
            if (n > 0) return Pending{};
            return absl::StrCat(x.x);
          };
        },
        [](Int x) {
          return []() mutable -> Poll<std::string> { return Pending{}; };
        },
        [](std::string x) { return x; });
  };
  auto promise = make_promise(V(Float{3.0f}));
  EXPECT_THAT(promise(), IsPending());
  EXPECT_THAT(promise(), IsPending());
  EXPECT_THAT(promise(), IsReady("3"));
  promise = make_promise(V(Int{42}));
  for (int i = 0; i < 10000; i++) {
    EXPECT_THAT(promise(), IsPending());
  }
  promise = make_promise(V("hello"));
  EXPECT_THAT(promise(), IsReady("hello"));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
