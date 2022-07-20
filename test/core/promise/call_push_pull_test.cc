// Copyright 2022 gRPC authors.
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

#include "src/core/lib/promise/call_push_pull.h"

#include <utility>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace grpc_core {

TEST(CallPushPullTest, Empty) {
  auto p = CallPushPull([] { return absl::OkStatus(); },
                        [] { return absl::OkStatus(); },
                        [] { return absl::OkStatus(); });
  EXPECT_EQ(p(), Poll<absl::Status>(absl::OkStatus()));
}

TEST(CallPushPullTest, Paused) {
  auto p = CallPushPull([]() -> Poll<absl::Status> { return Pending{}; },
                        []() -> Poll<absl::Status> { return Pending{}; },
                        []() -> Poll<absl::Status> { return Pending{}; });
  EXPECT_EQ(p(), Poll<absl::Status>(Pending{}));
}

TEST(CallPushPullTest, OneReady) {
  auto a = CallPushPull([]() -> Poll<absl::Status> { return absl::OkStatus(); },
                        []() -> Poll<absl::Status> { return Pending{}; },
                        []() -> Poll<absl::Status> { return Pending{}; });
  EXPECT_EQ(a(), Poll<absl::Status>(Pending{}));
  auto b = CallPushPull([]() -> Poll<absl::Status> { return Pending{}; },
                        []() -> Poll<absl::Status> { return absl::OkStatus(); },
                        []() -> Poll<absl::Status> { return Pending{}; });
  EXPECT_EQ(b(), Poll<absl::Status>(Pending{}));
  auto c =
      CallPushPull([]() -> Poll<absl::Status> { return Pending{}; },
                   []() -> Poll<absl::Status> { return Pending{}; },
                   []() -> Poll<absl::Status> { return absl::OkStatus(); });
  EXPECT_EQ(c(), Poll<absl::Status>(Pending{}));
}

TEST(CallPushPullTest, OneFailed) {
  auto a = CallPushPull(
      []() -> Poll<absl::Status> { return absl::UnknownError("bah"); },
      []() -> Poll<absl::Status> { return Pending{}; },
      []() -> Poll<absl::Status> { return Pending{}; });
  EXPECT_EQ(a(), Poll<absl::Status>(absl::UnknownError("bah")));
  auto b = CallPushPull(
      []() -> Poll<absl::Status> { return Pending{}; },
      []() -> Poll<absl::Status> { return absl::UnknownError("humbug"); },
      []() -> Poll<absl::Status> { return Pending{}; });
  EXPECT_EQ(b(), Poll<absl::Status>(absl::UnknownError("humbug")));
  auto c = CallPushPull(
      []() -> Poll<absl::Status> { return Pending{}; },
      []() -> Poll<absl::Status> { return Pending{}; },
      []() -> Poll<absl::Status> { return absl::UnknownError("wha"); });
  EXPECT_EQ(c(), Poll<absl::Status>(absl::UnknownError("wha")));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
