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

#include <gtest/gtest.h>

namespace grpc_core {

TEST(PromiseTest, SucceedAndThen) {
  EXPECT_EQ(TrySeq([] { return absl::StatusOr<int>(1); },
                   [](int i) {
                     return [i]() { return absl::StatusOr<int>(i + 1); };
                   })(),
            Poll<absl::StatusOr<int>>(absl::StatusOr<int>(2)));
}

TEST(PromiseTest, SucceedDirectlyAndThenDirectly) {
  EXPECT_EQ(
      TrySeq([] { return 1; }, [](int i) { return [i]() { return i + 1; }; })(),
      Poll<absl::StatusOr<int>>(absl::StatusOr<int>(2)));
}

TEST(PromiseTest, SucceedAndThenChangeType) {
  EXPECT_EQ(
      TrySeq([] { return absl::StatusOr<int>(42); },
             [](int i) {
               return [i]() {
                 return absl::StatusOr<std::string>(std::to_string(i));
               };
             })(),
      Poll<absl::StatusOr<std::string>>(absl::StatusOr<std::string>("42")));
}

TEST(PromiseTest, FailAndThen) {
  EXPECT_EQ(TrySeq([]() { return absl::StatusOr<int>(absl::CancelledError()); },
                   [](int) {
                     return []() -> Poll<absl::StatusOr<double>> { abort(); };
                   })(),
            Poll<absl::StatusOr<double>>(
                absl::StatusOr<double>(absl::CancelledError())));
}

TEST(PromiseTest, RawSucceedAndThen) {
  EXPECT_EQ(TrySeq([] { return absl::OkStatus(); },
                   [] { return []() { return absl::OkStatus(); }; })(),
            Poll<absl::Status>(absl::OkStatus()));
}

TEST(PromiseTest, RawFailAndThen) {
  EXPECT_EQ(TrySeq([] { return absl::CancelledError(); },
                   []() { return []() -> Poll<absl::Status> { abort(); }; })(),
            Poll<absl::Status>(absl::CancelledError()));
}

TEST(PromiseTest, RawSucceedAndThenValue) {
  EXPECT_EQ(TrySeq([] { return absl::OkStatus(); },
                   [] { return []() { return absl::StatusOr<int>(42); }; })(),
            Poll<absl::StatusOr<int>>(absl::StatusOr<int>(42)));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
