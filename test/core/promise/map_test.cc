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

#include "src/core/lib/promise/map.h"

#include "absl/functional/any_invocable.h"
#include "gtest/gtest.h"

#include "src/core/lib/promise/promise.h"

namespace grpc_core {

TEST(MapTest, Works) {
  Promise<int> x = Map([]() { return 42; }, [](int i) { return i / 2; });
  EXPECT_EQ(x(), Poll<int>(21));
}

TEST(MapTest, JustElem) {
  std::tuple<int, double> t(1, 3.2);
  EXPECT_EQ(JustElem<1>()(t), 3.2);
  EXPECT_EQ(JustElem<0>()(t), 1);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
