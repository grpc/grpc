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

#include "src/core/lib/promise/try_concurrently.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace grpc_core {

TEST(TryConcurrentlyTest, Immediate) {
  auto a = TryConcurrently([] { return absl::OkStatus(); });
  EXPECT_EQ(a(), Poll<absl::Status>(absl::OkStatus()));
  auto b = TryConcurrently([] { return absl::OkStatus(); }).NecessaryPush([] {
    return absl::OkStatus();
  });
  EXPECT_EQ(b(), Poll<absl::Status>(absl::OkStatus()));
  auto c = TryConcurrently([] { return absl::OkStatus(); }).NecessaryPull([] {
    return absl::OkStatus();
  });
  EXPECT_EQ(c(), Poll<absl::Status>(absl::OkStatus()));
  auto d = TryConcurrently([] {
             return absl::OkStatus();
           }).NecessaryPull([] {
               return absl::OkStatus();
             }).NecessaryPush([] { return absl::OkStatus(); });
  EXPECT_EQ(d(), Poll<absl::Status>(absl::OkStatus()));
  auto e = TryConcurrently([] {
             return absl::OkStatus();
           }).HelperPush([]() -> Poll<absl::Status> { return Pending{}; });
  EXPECT_EQ(e(), Poll<absl::Status>(absl::OkStatus()));
  auto f = TryConcurrently([] {
             return absl::OkStatus();
           }).HelperPull([]() -> Poll<absl::Status> { return Pending{}; });
  EXPECT_EQ(f(), Poll<absl::Status>(absl::OkStatus()));
}

TEST(TryConcurrentlyTest, Paused) {
  auto a = TryConcurrently([]() -> Poll<absl::Status> { return Pending{}; });
  EXPECT_EQ(a(), Poll<absl::Status>(Pending{}));
  auto b = TryConcurrently([]() {
             return absl::OkStatus();
           }).NecessaryPush([]() -> Poll<absl::Status> { return Pending{}; });
  EXPECT_EQ(b(), Poll<absl::Status>(Pending{}));
  auto c = TryConcurrently([]() {
             return absl::OkStatus();
           }).NecessaryPull([]() -> Poll<absl::Status> { return Pending{}; });
  EXPECT_EQ(c(), Poll<absl::Status>(Pending{}));
}

TEST(TryConcurrentlyTest, OneFailed) {
  auto a = TryConcurrently(
      []() -> Poll<absl::Status> { return absl::UnknownError("bah"); });
  EXPECT_EQ(a(), Poll<absl::Status>(absl::UnknownError("bah")));
  auto b = TryConcurrently([]() -> Poll<absl::Status> {
             return Pending{};
           }).NecessaryPush([] { return absl::UnknownError("humbug"); });
  EXPECT_EQ(b(), Poll<absl::Status>(absl::UnknownError("humbug")));
  auto c = TryConcurrently([]() -> Poll<absl::Status> {
             return Pending{};
           }).NecessaryPull([]() -> Poll<absl::Status> {
    return absl::UnknownError("wha");
  });
  EXPECT_EQ(c(), Poll<absl::Status>(absl::UnknownError("wha")));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
