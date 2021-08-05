/*
 *
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "include/grpc/impl/codegen/port_platform.h"

#include <gtest/gtest.h>
#include "src/core/ext/transport/chttp2/transport/popularity_count.h"

namespace grpc_core {
namespace testing {

TEST(PopularityCountTest, OneThing) {
  PopularityCount<4> pop;
  for (int i = 0; i < 1000; i++) {
    EXPECT_TRUE(pop.AddElement(0));
  }
}

TEST(PopularityCountTest, TwoThings) {
  for (int first = 2; first < 254; first++) {
    PopularityCount<4> pop;
    for (int i = 0; i < first; i++) {
      EXPECT_TRUE(pop.AddElement(0)) << "i = " << i << "; first = " << first;
    }
    for (int i = 0; i < 2 * first / (4 - 2); i++) {
      EXPECT_FALSE(pop.AddElement(1)) << "i = " << i << "; first = " << first;
    }
    EXPECT_TRUE(pop.AddElement(1)) << "first = " << first;
  }
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
