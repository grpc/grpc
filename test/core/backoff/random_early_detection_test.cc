// Copyright 2023 gRPC authors.
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

#include "src/core/util/random_early_detection.h"

#include <memory>

#include "absl/random/random.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

TEST(RandomEarlyDetectionTest, NoOp) {
  RandomEarlyDetection red(100, 200);
  EXPECT_EQ(red.soft_limit(), 100);
  EXPECT_EQ(red.hard_limit(), 200);
}

TEST(RandomEarlyDetectionTest, Distribution) {
  absl::BitGen bitgen;
  RandomEarlyDetection red(100, 200);
  int64_t counts[300] = {};
  for (int round = 0; round < 10000; round++) {
    for (int64_t i = 0; i < 300; i++) {
      if (red.Reject(i, absl::BitGenRef(bitgen))) counts[i]++;
    }
  }
  for (int64_t i = 0; i < 100; i++) {
    // [0, 100) should never be rejected
    EXPECT_EQ(counts[i], 0) << i;
    // [100, 200) should be rejected with probability ramping from 0 to 1
    EXPECT_GT(counts[i + 100], (i - 5) * 100) << i;
    EXPECT_LT(counts[i + 100], (i + 5) * 100) << i;
    // [200, 300) should always be rejected
    EXPECT_EQ(counts[i + 200], 10000) << i;
  }
}

TEST(RandomEarlyDetection, MustRejectWorks) {
  RandomEarlyDetection red(100, 200);
  for (int64_t i = 0; i < 200; i++) {
    EXPECT_FALSE(red.MustReject(i));
  }
  for (int64_t i = 200; i < 300; i++) {
    EXPECT_TRUE(red.MustReject(i));
  }
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
