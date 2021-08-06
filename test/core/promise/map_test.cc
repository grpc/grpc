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

#include <gtest/gtest.h>
#include "src/core/lib/promise/promise.h"

namespace grpc_core {

TEST(MapTest, Works) {
  Promise<int> x = Map([]() { return ready(42); }, [](int i) { return i / 2; });
  EXPECT_EQ(x().take(), 21);
}

TEST(MapTest, JustElem0) {
  Promise<int> x =
      Map([]() { return ready(std::make_tuple(1, 2, 3)); }, JustElem<0>());
  EXPECT_EQ(x().take(), 1);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
