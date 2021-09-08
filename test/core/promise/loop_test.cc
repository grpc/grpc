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

#include "src/core/lib/promise/loop.h"

#include <gtest/gtest.h>

namespace grpc_core {

TEST(LoopTest, CountToFive) {
  int i = 0;
  Loop([&i]() -> LoopCtl<int> {
    i++;
    if (i < 5) return Continue();
    return i;
  })();
  EXPECT_EQ(i, 5);
}

TEST(LoopTest, FactoryCountToFive) {
  int i = 0;
  Loop([&i]() {
    return [&i]() -> LoopCtl<int> {
      i++;
      if (i < 5) return Continue();
      return i;
    };
  })();
  EXPECT_EQ(i, 5);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
