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
  EXPECT_EQ(TrySeq([] { return ready(absl::StatusOr<int>(1)); },
                   [](int i) {
                     return [i]() { return ready(absl::StatusOr<int>(i + 1)); };
                   })()
                .take(),
            absl::StatusOr<int>(2));
}

TEST(PromiseTest, SucceedAndThenChangeType) {
  EXPECT_EQ(
      TrySeq([] { return ready(absl::StatusOr<int>(42)); },
             [](int i) {
               return [i]() {
                 return ready(absl::StatusOr<std::string>(std::to_string(i)));
               };
             })()
          .take(),
      absl::StatusOr<std::string>("42"));
}

TEST(PromiseTest, FailAndThen) {
  EXPECT_EQ(
      TrySeq(
          []() { return ready(absl::StatusOr<int>(absl::CancelledError())); },
          [](int i) {
            return []() -> Poll<absl::StatusOr<double>> { abort(); };
          })()
          .take(),
      absl::StatusOr<double>(absl::CancelledError()));
}

TEST(PromiseTest, RawSucceedAndThen) {
  EXPECT_EQ(TrySeq([] { return ready(absl::OkStatus()); },
                   [] { return []() { return ready(absl::OkStatus()); }; })()
                .take(),
            absl::OkStatus());
}

TEST(PromiseTest, RawFailAndThen) {
  EXPECT_EQ(TrySeq([] { return ready(absl::CancelledError()); },
                   []() { return []() -> Poll<absl::Status> { abort(); }; })()
                .take(),
            absl::CancelledError());
}

TEST(PromiseTest, RawSucceedAndThenValue) {
  EXPECT_EQ(
      TrySeq([] { return ready(absl::OkStatus()); },
             [] { return []() { return ready(absl::StatusOr<int>(42)); }; })()
          .take(),
      absl::StatusOr<int>(42));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
