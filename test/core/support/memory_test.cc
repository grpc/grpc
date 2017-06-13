/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/support/memory.h"
#include <gtest/gtest.h>
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

struct Foo {
  Foo(int p, int q) : a(p), b(q) {}
  int a;
  int b;
};

TEST(MemoryTest, NewDeleteTest) { Delete(New<int>()); }

TEST(MemoryTest, NewDeleteWithArgTest) {
  int* i = New<int>(42);
  EXPECT_EQ(42, *i);
  Delete(i);
}

TEST(MemoryTest, NewDeleteWithArgsTest) {
  Foo* p = New<Foo>(1, 2);
  EXPECT_EQ(1, p->a);
  EXPECT_EQ(2, p->b);
  Delete(p);
}

TEST(MemoryTest, MakeUniqueTest) { MakeUnique<int>(); }

TEST(MemoryTest, MakeUniqueWithArgTest) {
  auto i = MakeUnique<int>(42);
  EXPECT_EQ(42, *i);
}

TEST(MemoryTest, UniquePtrWithCustomDeleter) {
  int n = 0;
  class IncrementingDeleter {
   public:
    void operator()(int* p) { ++*p; }
  };
  {
    UniquePtr<int, IncrementingDeleter> p(&n);
    EXPECT_EQ(0, n);
  }
  EXPECT_EQ(1, n);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
