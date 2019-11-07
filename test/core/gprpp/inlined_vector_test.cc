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
  EXPECT_EQ(static_cast<size_t>(len), v->size());
  EXPECT_LE(static_cast<size_t>(len), v->capacity());
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
  InlinedVector<std::unique_ptr<int>, 1> v;
  std::unique_ptr<int> i = MakeUnique<int>(3);
  v.push_back(std::move(i));
  EXPECT_EQ(nullptr, i.get());
  EXPECT_EQ(1UL, v.size());
  EXPECT_EQ(3, *v[0]);
}

TEST(InlinedVectorTest, EmplaceBack) {
  InlinedVector<std::unique_ptr<int>, 1> v;
  v.emplace_back(new int(3));
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

TEST(InlinedVectorTest, EqualOperator) {
  constexpr int kNumElements = 10;
  // Both v1 and v2 are empty.
  InlinedVector<int, 5> v1;
  InlinedVector<int, 5> v2;
  EXPECT_TRUE(v1 == v2);
  // Both v1 and v2 contains the same data.
  FillVector(&v1, kNumElements);
  FillVector(&v2, kNumElements);
  EXPECT_TRUE(v1 == v2);
  // The sizes of v1 and v2 are different.
  v1.push_back(0);
  EXPECT_FALSE(v1 == v2);
  // The contents of v1 and v2 are different although their sizes are the same.
  v2.push_back(1);
  EXPECT_FALSE(v1 == v2);
}

TEST(InlinedVectorTest, NotEqualOperator) {
  constexpr int kNumElements = 10;
  // Both v1 and v2 are empty.
  InlinedVector<int, 5> v1;
  InlinedVector<int, 5> v2;
  EXPECT_FALSE(v1 != v2);
  // Both v1 and v2 contains the same data.
  FillVector(&v1, kNumElements);
  FillVector(&v2, kNumElements);
  EXPECT_FALSE(v1 != v2);
  // The sizes of v1 and v2 are different.
  v1.push_back(0);
  EXPECT_TRUE(v1 != v2);
  // The contents of v1 and v2 are different although their sizes are the same.
  v2.push_back(1);
  EXPECT_TRUE(v1 != v2);
}

// the following constants and typedefs are used for copy/move
// construction/assignment
const size_t kInlinedLength = 8;
typedef InlinedVector<int, kInlinedLength> IntVec8;
const size_t kInlinedFillSize = kInlinedLength - 1;
const size_t kAllocatedFillSize = kInlinedLength + 1;

TEST(InlinedVectorTest, CopyConstructorInlined) {
  IntVec8 original;
  FillVector(&original, kInlinedFillSize);
  IntVec8 copy_constructed(original);
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], copy_constructed[i]);
  }
}

TEST(InlinedVectorTest, CopyConstructorAllocated) {
  IntVec8 original;
  FillVector(&original, kAllocatedFillSize);
  IntVec8 copy_constructed(original);
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], copy_constructed[i]);
  }
}

TEST(InlinedVectorTest, CopyAssignementInlinedInlined) {
  IntVec8 original;
  FillVector(&original, kInlinedFillSize);
  IntVec8 copy_assigned;
  FillVector(&copy_assigned, kInlinedFillSize, 99);
  copy_assigned = original;
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], copy_assigned[i]);
  }
}

TEST(InlinedVectorTest, CopyAssignementInlinedAllocated) {
  IntVec8 original;
  FillVector(&original, kInlinedFillSize);
  IntVec8 copy_assigned;
  FillVector(&copy_assigned, kAllocatedFillSize, 99);
  copy_assigned = original;
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], copy_assigned[i]);
  }
}

TEST(InlinedVectorTest, CopyAssignementAllocatedInlined) {
  IntVec8 original;
  FillVector(&original, kAllocatedFillSize);
  IntVec8 copy_assigned;
  FillVector(&copy_assigned, kInlinedFillSize, 99);
  copy_assigned = original;
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], copy_assigned[i]);
  }
}

TEST(InlinedVectorTest, CopyAssignementAllocatedAllocated) {
  IntVec8 original;
  FillVector(&original, kAllocatedFillSize);
  IntVec8 copy_assigned;
  FillVector(&copy_assigned, kAllocatedFillSize, 99);
  copy_assigned = original;
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], copy_assigned[i]);
  }
}

TEST(InlinedVectorTest, MoveConstructorInlined) {
  IntVec8 original;
  FillVector(&original, kInlinedFillSize);
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
  IntVec8 original;
  FillVector(&original, kAllocatedFillSize);
  IntVec8 tmp(original);
  auto* old_data = tmp.data();
  IntVec8 move_constructed(std::move(tmp));
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], move_constructed[i]);
  }
  // original data was allocated, so it should been moved, not copied
  EXPECT_EQ(move_constructed.data(), old_data);
}

TEST(InlinedVectorTest, MoveAssignmentInlinedInlined) {
  IntVec8 original;
  FillVector(&original, kInlinedFillSize);
  IntVec8 move_assigned;
  FillVector(&move_assigned, kInlinedFillSize, 99);  // Add dummy elements
  IntVec8 tmp(original);
  auto* old_data = tmp.data();
  move_assigned = std::move(tmp);
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], move_assigned[i]);
  }
  // original data was inlined so it should have been copied, not moved.
  EXPECT_NE(move_assigned.data(), old_data);
}

TEST(InlinedVectorTest, MoveAssignmentInlinedAllocated) {
  IntVec8 original;
  FillVector(&original, kInlinedFillSize);
  IntVec8 move_assigned;
  FillVector(&move_assigned, kAllocatedFillSize, 99);  // Add dummy elements
  IntVec8 tmp(original);
  auto* old_data = tmp.data();
  move_assigned = std::move(tmp);
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], move_assigned[i]);
  }
  // original data was inlined so it should have been copied, not moved.
  EXPECT_NE(move_assigned.data(), old_data);
}

TEST(InlinedVectorTest, MoveAssignmentAllocatedInlined) {
  IntVec8 original;
  FillVector(&original, kAllocatedFillSize);
  IntVec8 move_assigned;
  FillVector(&move_assigned, kInlinedFillSize, 99);  // Add dummy elements
  IntVec8 tmp(original);
  auto* old_data = tmp.data();
  move_assigned = std::move(tmp);
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], move_assigned[i]);
  }
  // original data was allocated so it should have been moved, not copied.
  EXPECT_EQ(move_assigned.data(), old_data);
}

TEST(InlinedVectorTest, MoveAssignmentAllocatedAllocated) {
  IntVec8 original;
  FillVector(&original, kAllocatedFillSize);
  IntVec8 move_assigned;
  FillVector(&move_assigned, kAllocatedFillSize, 99);  // Add dummy elements
  IntVec8 tmp(original);
  auto* old_data = tmp.data();
  move_assigned = std::move(tmp);
  for (size_t i = 0; i < original.size(); ++i) {
    EXPECT_EQ(original[i], move_assigned[i]);
  }
  // original data was allocated so it should have been moved, not copied.
  EXPECT_EQ(move_assigned.data(), old_data);
}

// A copyable and movable value class, used to test that elements' copy
// and move methods are called correctly.
class Value {
 public:
  explicit Value(int v) : value_(MakeUnique<int>(v)) {}

  // copyable
  Value(const Value& v) {
    value_ = MakeUnique<int>(*v.value_);
    copied_ = true;
  }
  Value& operator=(const Value& v) {
    value_ = MakeUnique<int>(*v.value_);
    copied_ = true;
    return *this;
  }

  // movable
  Value(Value&& v) {
    value_ = std::move(v.value_);
    moved_ = true;
  }
  Value& operator=(Value&& v) {
    value_ = std::move(v.value_);
    moved_ = true;
    return *this;
  }

  const std::unique_ptr<int>& value() const { return value_; }
  bool copied() const { return copied_; }
  bool moved() const { return moved_; }

 private:
  std::unique_ptr<int> value_;
  bool copied_ = false;
  bool moved_ = false;
};

TEST(InlinedVectorTest, CopyConstructorCopiesElementsInlined) {
  InlinedVector<Value, 1> v1;
  v1.emplace_back(3);
  InlinedVector<Value, 1> v2(v1);
  EXPECT_EQ(v2.size(), 1UL);
  EXPECT_EQ(*v2[0].value(), 3);
  // Addresses should differ.
  EXPECT_NE(v1[0].value().get(), v2[0].value().get());
  EXPECT_TRUE(v2[0].copied());
}

TEST(InlinedVectorTest, CopyConstructorCopiesElementsAllocated) {
  InlinedVector<Value, 1> v1;
  v1.reserve(2);
  v1.emplace_back(3);
  v1.emplace_back(5);
  InlinedVector<Value, 1> v2(v1);
  EXPECT_EQ(v2.size(), 2UL);
  EXPECT_EQ(*v2[0].value(), 3);
  EXPECT_EQ(*v2[1].value(), 5);
  // Addresses should differ.
  EXPECT_NE(v1[0].value().get(), v2[0].value().get());
  EXPECT_NE(v1[1].value().get(), v2[1].value().get());
  EXPECT_TRUE(v2[0].copied());
  EXPECT_TRUE(v2[1].copied());
}

TEST(InlinedVectorTest, CopyAssignmentCopiesElementsInlined) {
  InlinedVector<Value, 1> v1;
  v1.emplace_back(3);
  InlinedVector<Value, 1> v2;
  EXPECT_EQ(v2.size(), 0UL);
  v2 = v1;
  EXPECT_EQ(v2.size(), 1UL);
  EXPECT_EQ(*v2[0].value(), 3);
  // Addresses should differ.
  EXPECT_NE(v1[0].value().get(), v2[0].value().get());
  EXPECT_TRUE(v2[0].copied());
}

TEST(InlinedVectorTest, CopyAssignmentCopiesElementsAllocated) {
  InlinedVector<Value, 1> v1;
  v1.reserve(2);
  v1.emplace_back(3);
  v1.emplace_back(5);
  InlinedVector<Value, 1> v2;
  EXPECT_EQ(v2.size(), 0UL);
  v2 = v1;
  EXPECT_EQ(v2.size(), 2UL);
  EXPECT_EQ(*v2[0].value(), 3);
  EXPECT_EQ(*v2[1].value(), 5);
  // Addresses should differ.
  EXPECT_NE(v1[0].value().get(), v2[0].value().get());
  EXPECT_NE(v1[1].value().get(), v2[1].value().get());
  EXPECT_TRUE(v2[0].copied());
  EXPECT_TRUE(v2[1].copied());
}

TEST(InlinedVectorTest, MoveConstructorMovesElementsInlined) {
  InlinedVector<Value, 1> v1;
  v1.emplace_back(3);
  int* addr = v1[0].value().get();
  InlinedVector<Value, 1> v2(std::move(v1));
  EXPECT_EQ(v2.size(), 1UL);
  EXPECT_EQ(*v2[0].value(), 3);
  EXPECT_EQ(addr, v2[0].value().get());
  EXPECT_TRUE(v2[0].moved());
}

TEST(InlinedVectorTest, MoveConstructorMovesElementsAllocated) {
  InlinedVector<Value, 1> v1;
  v1.reserve(2);
  v1.emplace_back(3);
  v1.emplace_back(5);
  int* addr1 = v1[0].value().get();
  int* addr2 = v1[1].value().get();
  Value* data1 = v1.data();
  InlinedVector<Value, 1> v2(std::move(v1));
  EXPECT_EQ(v2.size(), 2UL);
  EXPECT_EQ(*v2[0].value(), 3);
  EXPECT_EQ(*v2[1].value(), 5);
  EXPECT_EQ(addr1, v2[0].value().get());
  EXPECT_EQ(addr2, v2[1].value().get());
  // In this case, elements won't be moved, because we have just stolen
  // the underlying storage.
  EXPECT_EQ(data1, v2.data());
}

TEST(InlinedVectorTest, MoveAssignmentMovesElementsInlined) {
  InlinedVector<Value, 1> v1;
  v1.emplace_back(3);
  int* addr = v1[0].value().get();
  InlinedVector<Value, 1> v2;
  EXPECT_EQ(v2.size(), 0UL);
  v2 = std::move(v1);
  EXPECT_EQ(v2.size(), 1UL);
  EXPECT_EQ(*v2[0].value(), 3);
  EXPECT_EQ(addr, v2[0].value().get());
  EXPECT_TRUE(v2[0].moved());
}

TEST(InlinedVectorTest, MoveAssignmentMovesElementsAllocated) {
  InlinedVector<Value, 1> v1;
  v1.reserve(2);
  v1.emplace_back(3);
  v1.emplace_back(5);
  int* addr1 = v1[0].value().get();
  int* addr2 = v1[1].value().get();
  Value* data1 = v1.data();
  InlinedVector<Value, 1> v2;
  EXPECT_EQ(v2.size(), 0UL);
  v2 = std::move(v1);
  EXPECT_EQ(v2.size(), 2UL);
  EXPECT_EQ(*v2[0].value(), 3);
  EXPECT_EQ(*v2[1].value(), 5);
  EXPECT_EQ(addr1, v2[0].value().get());
  EXPECT_EQ(addr2, v2[1].value().get());
  // In this case, elements won't be moved, because we have just stolen
  // the underlying storage.
  EXPECT_EQ(data1, v2.data());
}

TEST(InlinedVectorTest, PopBackInlined) {
  InlinedVector<std::unique_ptr<int>, 2> v;
  // Add two elements, pop one out
  v.push_back(MakeUnique<int>(3));
  EXPECT_EQ(1UL, v.size());
  EXPECT_EQ(3, *v[0]);
  v.push_back(MakeUnique<int>(5));
  EXPECT_EQ(2UL, v.size());
  EXPECT_EQ(5, *v[1]);
  v.pop_back();
  EXPECT_EQ(1UL, v.size());
}

TEST(InlinedVectorTest, PopBackAllocated) {
  const int kInlinedSize = 2;
  InlinedVector<std::unique_ptr<int>, kInlinedSize> v;
  // Add elements to ensure allocated backing.
  for (size_t i = 0; i < kInlinedSize + 1; ++i) {
    v.push_back(MakeUnique<int>(3));
    EXPECT_EQ(i + 1, v.size());
  }
  size_t sz = v.size();
  v.pop_back();
  EXPECT_EQ(sz - 1, v.size());
}

TEST(InlinedVectorTest, Resize) {
  const int kInlinedSize = 2;
  InlinedVector<std::unique_ptr<int>, kInlinedSize> v;
  // Size up.
  v.resize(5);
  EXPECT_EQ(5UL, v.size());
  EXPECT_EQ(nullptr, v[4]);
  // Size down.
  v[4] = MakeUnique<int>(5);
  v.resize(1);
  EXPECT_EQ(1UL, v.size());
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
