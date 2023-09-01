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

#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace grpc_core {

TEST(TrySeqTest, SucceedAndThen) {
  EXPECT_EQ(TrySeq([] { return absl::StatusOr<int>(1); },
                   [](int i) {
                     return [i]() { return absl::StatusOr<int>(i + 1); };
                   })(),
            Poll<absl::StatusOr<int>>(absl::StatusOr<int>(2)));
}

TEST(TrySeqTest, SucceedDirectlyAndThenDirectly) {
  EXPECT_EQ(
      TrySeq([] { return 1; }, [](int i) { return [i]() { return i + 1; }; })(),
      Poll<absl::StatusOr<int>>(absl::StatusOr<int>(2)));
}

TEST(TrySeqTest, SucceedAndThenChangeType) {
  EXPECT_EQ(
      TrySeq([] { return absl::StatusOr<int>(42); },
             [](int i) {
               return [i]() {
                 return absl::StatusOr<std::string>(std::to_string(i));
               };
             })(),
      Poll<absl::StatusOr<std::string>>(absl::StatusOr<std::string>("42")));
}

TEST(TrySeqTest, FailAndThen) {
  EXPECT_EQ(TrySeq([]() { return absl::StatusOr<int>(absl::CancelledError()); },
                   [](int) {
                     return []() -> Poll<absl::StatusOr<double>> { abort(); };
                   })(),
            Poll<absl::StatusOr<double>>(
                absl::StatusOr<double>(absl::CancelledError())));
}

TEST(TrySeqTest, RawSucceedAndThen) {
  EXPECT_EQ(TrySeq([] { return absl::OkStatus(); },
                   [] { return []() { return absl::OkStatus(); }; })(),
            Poll<absl::Status>(absl::OkStatus()));
}

TEST(TrySeqTest, RawFailAndThen) {
  EXPECT_EQ(TrySeq([] { return absl::CancelledError(); },
                   []() { return []() -> Poll<absl::Status> { abort(); }; })(),
            Poll<absl::Status>(absl::CancelledError()));
}

TEST(TrySeqTest, RawSucceedAndThenValue) {
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
