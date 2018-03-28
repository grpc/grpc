/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/lib/gprpp/fixed_capacity_vector.h"
#include <gtest/gtest.h>
#include "src/core/lib/gprpp/memory.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

TEST(FixedCapacityVectorTest, CreateAndIterate) {
  const int kNumElements = 9;
  auto v = FixedCapacityVector<int>::Create(kNumElements);
  EXPECT_EQ(static_cast<size_t>(kNumElements), v->capacity());
  EXPECT_EQ(0UL, v->size());
  for (int i = 0; i < kNumElements; ++i) {
    v->push_back(i);
    EXPECT_EQ(i + 1UL, v->size());
  }
  EXPECT_EQ(static_cast<size_t>(kNumElements), v->size());
  for (int i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(i, (*v)[i]);
  }
}

TEST(FixedCapacityVectorTest, PushBackWithMove) {
  auto v = FixedCapacityVector<UniquePtr<int>>::Create(1);
  UniquePtr<int> i = MakeUnique<int>(3);
  v->push_back(std::move(i));
  EXPECT_EQ(nullptr, i.get());
  EXPECT_EQ(1UL, v->size());
  EXPECT_EQ(3, *(*v)[0]);
}

TEST(FixedCapacityVectorTest, EmplaceBack) {
  auto v = FixedCapacityVector<UniquePtr<int>>::Create(1);
  v->emplace_back(New<int>(3));
  EXPECT_EQ(1UL, v->size());
  EXPECT_EQ(3, *(*v)[0]);
}

TEST(FixedCapacityVectorTest, ClearAndRepopulate) {
  const int kNumElements = 5;
  auto v = FixedCapacityVector<int>::Create(kNumElements);
  EXPECT_EQ(0UL, v->size());
  for (int i = 0; i < kNumElements; ++i) {
    v->push_back(i);
    EXPECT_EQ(i + 1UL, v->size());
  }
  for (int i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(i, (*v)[i]);
  }
  v->clear();
  EXPECT_EQ(0UL, v->size());
  for (int i = 0; i < kNumElements; ++i) {
    v->push_back(kNumElements + i);
    EXPECT_EQ(i + 1UL, v->size());
  }
  for (int i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(kNumElements + i, (*v)[i]);
  }
}

TEST(FixedCapacityVectorTest, ConstIndexOperator) {
  constexpr int kNumElements = 10;
  auto v = FixedCapacityVector<int>::Create(kNumElements);
  EXPECT_EQ(0UL, v->size());
  for (int i = 0; i < kNumElements; ++i) {
    v->push_back(i);
    EXPECT_EQ(i + 1UL, v->size());
  }
  // The following lambda function is exceptionally allowed to use an anonymous
  // capture due to the erroneous behavior of the MSVC compiler, that refuses to
  // capture the kNumElements constexpr, something allowed by the standard.
  auto const_func = [&](const FixedCapacityVector<int>& v) {
    for (int i = 0; i < kNumElements; ++i) {
      EXPECT_EQ(i, v[i]);
    }
  };
  const_func(*v);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
