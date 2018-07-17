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

#include "src/core/lib/gprpp/inlined_vector.h"
#include <grpc/support/log.h>
#include <gtest/gtest.h>
#include "src/core/lib/gprpp/memory.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

template <typename Vector>
static void FillVector(Vector* v, int len, int start = 0) {
  for (int i = 0; i < len; i++) {
    v->push_back(i + start);
    EXPECT_EQ(i + 1UL, v->size());
  }
}

}  // namespace

TEST(InlinedVectorTest, CreateAndIterate) {
  const int kNumElements = 9;
  InlinedVector<int, 2> v;
  EXPECT_TRUE(v.empty());
  FillVector(&v, kNumElements);
  EXPECT_EQ(static_cast<size_t>(kNumElements), v.size());
  EXPECT_FALSE(v.empty());
  for (int i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(i, v[i]);
    EXPECT_EQ(i, &v[i] - &v[0]);  // Ensure contiguous allocation.
  }
}

TEST(InlinedVectorTest, ValuesAreInlined) {
  const int kNumElements = 5;
  InlinedVector<int, 10> v;
  FillVector(&v, kNumElements);
  EXPECT_EQ(static_cast<size_t>(kNumElements), v.size());
  for (int i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(i, v[i]);
  }
}

TEST(InlinedVectorTest, PushBackWithMove) {
  InlinedVector<UniquePtr<int>, 1> v;
  UniquePtr<int> i = MakeUnique<int>(3);
  v.push_back(std::move(i));
  EXPECT_EQ(nullptr, i.get());
  EXPECT_EQ(1UL, v.size());
  EXPECT_EQ(3, *v[0]);
}

TEST(InlinedVectorTest, EmplaceBack) {
  InlinedVector<UniquePtr<int>, 1> v;
  v.emplace_back(New<int>(3));
  EXPECT_EQ(1UL, v.size());
  EXPECT_EQ(3, *v[0]);
}

TEST(InlinedVectorTest, ClearAndRepopulate) {
  const int kNumElements = 10;
  InlinedVector<int, 5> v;
  EXPECT_EQ(0UL, v.size());
  FillVector(&v, kNumElements);
  for (int i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(i, v[i]);
  }
  v.clear();
  EXPECT_EQ(0UL, v.size());
  FillVector(&v, kNumElements, kNumElements);
  for (int i = 0; i < kNumElements; ++i) {
    EXPECT_EQ(kNumElements + i, v[i]);
  }
}

TEST(InlinedVectorTest, ConstIndexOperator) {
  constexpr int kNumElements = 10;
  InlinedVector<int, 5> v;
  EXPECT_EQ(0UL, v.size());
  FillVector(&v, kNumElements);
  // The following lambda function is exceptionally allowed to use an anonymous
  // capture due to the erroneous behavior of the MSVC compiler, that refuses to
  // capture the kNumElements constexpr, something allowed by the standard.
  auto const_func = [&](const InlinedVector<int, 5>& v) {
    for (int i = 0; i < kNumElements; ++i) {
      EXPECT_EQ(i, v[i]);
    }
  };
  const_func(v);
}

TEST(InlinedVectorTest, CopyConstructerInlined) {
  const size_t kInlinedLength = 8;
  const size_t kFillSize = kInlinedLength - 1;
  typedef InlinedVector<int, kInlinedLength> IntVec8;
  IntVec8 original;
  FillVector(&original, kFillSize);
  EXPECT_EQ(kFillSize, original.size());
  EXPECT_LE(kFillSize, original.capacity());
  IntVec8 copy_constructed(original);
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], copy_constructed[i]);
  }
}

TEST(InlinedVectorTest, CopyConstructerAllocated) {
  const size_t kInlinedLength = 8;
  const size_t kFillSize = kInlinedLength + 1;
  typedef InlinedVector<int, kInlinedLength> IntVec8;
  IntVec8 original;
  FillVector(&original, kFillSize);
  EXPECT_EQ(kFillSize, original.size());
  EXPECT_LE(kFillSize, original.capacity());
  IntVec8 copy_constructed(original);
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], copy_constructed[i]);
  }
}

TEST(InlinedVectorTest, CopyAssignementInlined) {
  const size_t kInlinedLength = 8;
  const size_t kFillSize = kInlinedLength - 1;
  typedef InlinedVector<int, kInlinedLength> IntVec8;
  IntVec8 original;
  FillVector(&original, kFillSize);
  EXPECT_EQ(kFillSize, original.size());
  EXPECT_LE(kFillSize, original.capacity());
  // copy assigned vector is inlined
  {
    IntVec8 copy_assigned;
    FillVector(&copy_assigned, kInlinedLength - 1, 99);
    copy_assigned = original;
    for (size_t i = 0; i < original.size(); ++i) {
      EXPECT_EQ(original[i], copy_assigned[i]);
    }
  }
  // copy assigned vector is allocated
  {
    IntVec8 copy_assigned;
    FillVector(&copy_assigned, kInlinedLength + 1, 99);
    copy_assigned = original;
    for (size_t i = 0; i < original.size(); ++i) {
      EXPECT_EQ(original[i], copy_assigned[i]);
    }
  }
}

TEST(InlinedVectorTest, CopyAssignementAllocated) {
  const size_t kInlinedLength = 8;
  const size_t kFillSize = kInlinedLength + 1;
  typedef InlinedVector<int, kInlinedLength> IntVec8;
  IntVec8 original;
  FillVector(&original, kFillSize);
  EXPECT_EQ(kFillSize, original.size());
  EXPECT_LE(kFillSize, original.capacity());
  // copy assigned vector is inlined
  {
    IntVec8 copy_assigned;
    FillVector(&copy_assigned, kInlinedLength - 1, 99);
    copy_assigned = original;
    for (size_t i = 0; i < original.size(); ++i) {
      EXPECT_EQ(original[i], copy_assigned[i]);
    }
  }
  // copy assigned vector is allocated
  {
    IntVec8 copy_assigned;
    FillVector(&copy_assigned, kInlinedLength + 1, 99);
    copy_assigned = original;
    for (size_t i = 0; i < original.size(); ++i) {
      EXPECT_EQ(original[i], copy_assigned[i]);
    }
  }
}

TEST(InlinedVectorTest, MoveConstructorInlined) {
  const size_t kInlinedLength = 8;
  const size_t kFillSize = kInlinedLength - 1;
  typedef InlinedVector<int, kInlinedLength> IntVec8;
  IntVec8 original;
  FillVector(&original, kFillSize);
  EXPECT_EQ(kFillSize, original.size());
  EXPECT_LE(kFillSize, original.capacity());
  IntVec8 tmp(original);
  auto* old_data = tmp.data();
  IntVec8 move_constructed(std::move(tmp));
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], move_constructed[i]);
  }
  // original data was inlined so it should have been copied, not moved.
  EXPECT_NE(move_constructed.data(), old_data);
}

TEST(InlinedVectorTest, MoveConstructorAllocated) {
  const size_t kInlinedLength = 8;
  const size_t kFillSize = kInlinedLength + 1;
  typedef InlinedVector<int, kInlinedLength> IntVec8;
  IntVec8 original;
  FillVector(&original, kFillSize);
  EXPECT_EQ(kFillSize, original.size());
  EXPECT_LE(kFillSize, original.capacity());
  IntVec8 tmp(original);
  auto* old_data = tmp.data();
  IntVec8 move_constructed(std::move(tmp));
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], move_constructed[i]);
  }
  // original data was allocated, so it should been moved, not copied
  EXPECT_EQ(move_constructed.data(), old_data);
}

TEST(InlinedVectorTest, MoveAssignmentInlined) {
  const size_t kInlinedLength = 8;
  const size_t kFillSize = kInlinedLength - 1;
  typedef InlinedVector<int, kInlinedLength> IntVec8;
  IntVec8 original;
  FillVector(&original, kFillSize);
  EXPECT_EQ(kFillSize, original.size());
  EXPECT_LE(kFillSize, original.capacity());
  // move assigned vector is inlined
  {
    IntVec8 move_assigned;
    FillVector(&move_assigned, kInlinedLength - 1, 99);  // Add dummy elements
    IntVec8 tmp(original);
    auto* old_data = tmp.data();
    move_assigned = std::move(tmp);
    for (size_t i = 0; i < original.size(); ++i) {
      EXPECT_EQ(original[i], move_assigned[i]);
    }
    // original data was inlined so it should have been copied, not moved.
    EXPECT_NE(move_assigned.data(), old_data);
  }
  // move assigned vector is allocated
  {
    IntVec8 move_assigned;
    FillVector(&move_assigned, kInlinedLength + 1, 99);  // Add dummy elements
    IntVec8 tmp(original);
    auto* old_data = tmp.data();
    move_assigned = std::move(tmp);
    for (size_t i = 0; i < original.size(); ++i) {
      EXPECT_EQ(original[i], move_assigned[i]);
    }
    // original data was inlined so it should have been copied, not moved.
    EXPECT_NE(move_assigned.data(), old_data);
  }
}

TEST(InlinedVectorTest, MoveAssignmentAllocated) {
  const size_t kInlinedLength = 8;
  const size_t kFillSize = kInlinedLength + 1;
  typedef InlinedVector<int, kInlinedLength> IntVec8;
  IntVec8 original;
  FillVector(&original, kFillSize);
  EXPECT_EQ(kFillSize, original.size());
  EXPECT_LE(kFillSize, original.capacity());
  // move assigned vector is inlined
  {
    IntVec8 move_assigned;
    FillVector(&move_assigned, kInlinedLength - 1, 99);  // Add dummy elements
    IntVec8 tmp(original);
    auto* old_data = tmp.data();
    move_assigned = std::move(tmp);
    for (size_t i = 0; i < original.size(); ++i) {
      EXPECT_EQ(original[i], move_assigned[i]);
    }
    // original data was allocated so it should have been moved, not copied.
    EXPECT_EQ(move_assigned.data(), old_data);
  }
  // move assigned vector is allocated
  {
    IntVec8 move_assigned;
    FillVector(&move_assigned, kInlinedLength + 1, 99);  // Add dummy elements
    IntVec8 tmp(original);
    auto* old_data = tmp.data();
    move_assigned = std::move(tmp);
    for (size_t i = 0; i < original.size(); ++i) {
      EXPECT_EQ(original[i], move_assigned[i]);
    }
    // original data was allocated so it should have been moved, not copied.
    EXPECT_EQ(move_assigned.data(), old_data);
  }
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
