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

#include "src/core/lib/support/vector.h"
#include <gtest/gtest.h>
#include "src/core/lib/support/memory.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

TEST(InlinedVectorTest, CreateAndIterate) {
  const int kNumElements = 9;
  InlinedVector<int, 2> v;
  for (int i = 0; i < kNumElements; ++i) {
    v.push_back(i);
  }
  EXPECT_EQ(static_cast<size_t>(kNumElements), v.size());
  for (int i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(i, v[i]);
  }
}

TEST(InlinedVectorTest, ValuesAreInlined) {
  const int kNumElements = 5;
  InlinedVector<int, 10> v;
  for (int i = 0; i < kNumElements; ++i) {
    v.push_back(i);
  }
  EXPECT_EQ(static_cast<size_t>(kNumElements), v.size());
  for (int i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(i, v[i]);
  }
}

TEST(InlinedVectorTest, PushBackWithMove) {
  InlinedVector<UniquePtr<int>, 1> v;
  UniquePtr<int> i = MakeUnique<int>(3);
  v.push_back(std::move(i));
  EXPECT_EQ(1, v.size());
  EXPECT_EQ(3, *v[0]);
}

TEST(InlinedVectorTest, EmplaceBack) {
  InlinedVector<UniquePtr<int>, 1> v;
  v.emplace_back(new int(3));
  EXPECT_EQ(1, v.size());
  EXPECT_EQ(3, *v[0]);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
