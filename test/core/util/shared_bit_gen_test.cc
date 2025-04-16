// Copyright 2025 gRPC authors.
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

#include "src/core/util/shared_bit_gen.h"

#include <stdint.h>

#include "absl/random/distributions.h"
#include "gtest/gtest.h"

namespace grpc_core {

TEST(SharedBitGenTest, Works) {
  SharedBitGen gen;
  for (int i = 0; i < 100; ++i) {
    auto x = absl::Uniform(gen, 0, 100);
    EXPECT_GE(x, 0);
    EXPECT_LT(x, 100);
  }
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
