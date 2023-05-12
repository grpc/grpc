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

#include "test/core/util/osa_distance.h"

#include "gtest/gtest.h"

namespace grpc_core {

TEST(OsaDistanceTest, Works) {
  EXPECT_EQ(OsaDistance("", ""), 0);
  EXPECT_EQ(OsaDistance("a", "a"), 0);
  EXPECT_EQ(OsaDistance("abc", "abc"), 0);
  EXPECT_EQ(OsaDistance("abc", "abd"), 1);
  EXPECT_EQ(OsaDistance("abc", "acb"), 1);
  EXPECT_EQ(OsaDistance("abcd", "acbd"), 1);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
