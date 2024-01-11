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

#include <stdint.h>
#include <string.h>

#include <memory>

#include "gtest/gtest.h"

#include <grpc/support/alloc.h>

#include "test/core/util/test_config.h"

TEST(AllocTest, MallocAligned) {
  for (size_t size = 1; size <= 256; ++size) {
    void* ptr = gpr_malloc_aligned(size, 16);
    ASSERT_NE(ptr, nullptr);
    ASSERT_EQ(((intptr_t)ptr & 0xf), 0);
    memset(ptr, 0, size);
    gpr_free_aligned(ptr);
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
