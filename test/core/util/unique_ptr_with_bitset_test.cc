//
//
// Copyright 2015 gRPC authors.
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
//
//

#include "src/core/util/unique_ptr_with_bitset.h"

#include <stdint.h>

#include <limits>
#include <memory>

#include "gtest/gtest.h"

#include <grpc/support/port_platform.h>

namespace grpc_core {

TEST(UniquePtrWithBitsetTest, Basic) {
  UniquePtrWithBitset<int, 1> ptr;
  EXPECT_EQ(ptr.get(), nullptr);
  EXPECT_EQ(ptr.TestBit(0), false);
  ptr.reset(new int(42));
  EXPECT_EQ(*ptr, 42);
  EXPECT_EQ(ptr.TestBit(0), false);
  ptr.SetBit(0);
  EXPECT_EQ(ptr.TestBit(0), true);
  ptr.reset();
  EXPECT_EQ(ptr.get(), nullptr);
  EXPECT_EQ(ptr.TestBit(0), true);
  ptr.ClearBit(0);
  EXPECT_EQ(ptr.TestBit(0), false);
  ptr.reset(new int(43));
  ptr.SetBit(0);

  UniquePtrWithBitset<int, 1> ptr2;
  ptr2 = std::move(ptr);
  EXPECT_EQ(*ptr2, 43);
  EXPECT_EQ(ptr2.TestBit(0), true);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
