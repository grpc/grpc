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

#include "src/core/lib/gprpp/map.h"

#include <gtest/gtest.h>

#include "include/grpc/support/string_util.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
class Payload {
 public:
  Payload() : data_(-1) {}
  explicit Payload(int data) : data_(data) {}
  Payload(const Payload& other) : data_(other.data_) {}
  Payload& operator=(const Payload& other) {
    if (this != &other) {
      data_ = other.data_;
    }
    return *this;
  }
  int data() { return data_; }

 private:
  int data_;
};

inline UniquePtr<char> CopyString(const char* string) {
  return UniquePtr<char>(gpr_strdup(string));
}

static constexpr char kKeys[][4] = {"abc", "efg", "hij", "klm", "xyz"};

class MapTest : public ::testing::Test {
 public:
  template <class Key, class T, class Compare>
  typename ::grpc_core::Map<Key, T, Compare>::Entry* Root(
      typename ::grpc_core::Map<Key, T, Compare>* map) {
    return map->root_;
  }
};

// Test insertion of Payload
TEST_F(MapTest, EmplaceAndFind) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(kKeys[i], Payload(i));
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map.find(kKeys[i])->second.data());
  }
}

// Test insertion of Payload Unique Ptrs
TEST_F(MapTest, EmplaceAndFindWithUniquePtrValue) {
  Map<const char*, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(kKeys[i], MakeUnique<Payload>(i));
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map.find(kKeys[i])->second->data());
  }
}

// Test insertion of Unique Ptr kKeys and Payload
TEST_F(MapTest, EmplaceAndFindWithUniquePtrKey) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(kKeys[i]), Payload(i));
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map.find(CopyString(kKeys[i]))->second.data());
  }
}

// Test insertion of Payload
TEST_F(MapTest, InsertAndFind) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.insert(MakePair(kKeys[i], Payload(i)));
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map.find(kKeys[i])->second.data());
  }
}

// Test insertion of Payload Unique Ptrs
TEST_F(MapTest, InsertAndFindWithUniquePtrValue) {
  Map<const char*, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.insert(MakePair(kKeys[i], MakeUnique<Payload>(i)));
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map.find(kKeys[i])->second->data());
  }
}

// Test insertion of Unique Ptr kKeys and Payload
TEST_F(MapTest, InsertAndFindWithUniquePtrKey) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.insert(MakePair(CopyString(kKeys[i]), Payload(i)));
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map.find(CopyString(kKeys[i]))->second.data());
  }
}

// Test bracket operators
TEST_F(MapTest, BracketOperator) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map[kKeys[i]] = Payload(i);
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map[kKeys[i]].data());
  }
}

// Test bracket operators with unique pointer to payload
TEST_F(MapTest, BracketOperatorWithUniquePtrValue) {
  Map<const char*, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map[kKeys[i]] = MakeUnique<Payload>(i);
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map[kKeys[i]]->data());
  }
}

// Test bracket operators with unique pointer to payload
TEST_F(MapTest, BracketOperatorWithUniquePtrKey) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map[CopyString(kKeys[i])] = Payload(i);
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map[CopyString(kKeys[i])].data());
  }
}

// Test removal of a single value
TEST_F(MapTest, Erase) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(kKeys[i], Payload(i));
  }
  EXPECT_EQ(test_map.size(), 5UL);
  EXPECT_EQ(test_map.erase(kKeys[3]), 1UL);  // Remove "hij"
  for (int i = 0; i < 5; i++) {
    if (i == 3) {  // "hij" should not be present
      EXPECT_TRUE(test_map.find(kKeys[i]) == test_map.end());
    } else {
      EXPECT_EQ(i, test_map.find(kKeys[i])->second.data());
    }
  }
  EXPECT_EQ(test_map.size(), 4UL);
}

// Test removal of a single value with unique ptr to payload
TEST_F(MapTest, EraseWithUniquePtrValue) {
  Map<const char*, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(kKeys[i], MakeUnique<Payload>(i));
  }
  EXPECT_EQ(test_map.size(), 5UL);
  test_map.erase(kKeys[3]);  // Remove "hij"
  for (int i = 0; i < 5; i++) {
    if (i == 3) {  // "hij" should not be present
      EXPECT_TRUE(test_map.find(kKeys[i]) == test_map.end());
    } else {
      EXPECT_EQ(i, test_map.find(kKeys[i])->second->data());
    }
  }
  EXPECT_EQ(test_map.size(), 4UL);
}

// Test removal of a single value
TEST_F(MapTest, EraseWithUniquePtrKey) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(kKeys[i]), Payload(i));
  }
  EXPECT_EQ(test_map.size(), 5UL);
  test_map.erase(CopyString(kKeys[3]));  // Remove "hij"
  for (int i = 0; i < 5; i++) {
    if (i == 3) {  // "hij" should not be present
      EXPECT_TRUE(test_map.find(CopyString(kKeys[i])) == test_map.end());
    } else {
      EXPECT_EQ(i, test_map.find(CopyString(kKeys[i]))->second.data());
    }
  }
  EXPECT_EQ(test_map.size(), 4UL);
}

// Test clear
TEST_F(MapTest, SizeAndClear) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(kKeys[i], Payload(i));
  }
  EXPECT_EQ(test_map.size(), 5UL);
  EXPECT_FALSE(test_map.empty());
  test_map.clear();
  EXPECT_EQ(test_map.size(), 0UL);
  EXPECT_TRUE(test_map.empty());
}

// Test clear with unique ptr payload
TEST_F(MapTest, SizeAndClearWithUniquePtrValue) {
  Map<const char*, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(kKeys[i], MakeUnique<Payload>(i));
  }
  EXPECT_EQ(test_map.size(), 5UL);
  EXPECT_FALSE(test_map.empty());
  test_map.clear();
  EXPECT_EQ(test_map.size(), 0UL);
  EXPECT_TRUE(test_map.empty());
}

// Test clear with unique ptr char key
TEST_F(MapTest, SizeAndClearWithUniquePtrKey) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(kKeys[i]), Payload(i));
  }
  EXPECT_EQ(test_map.size(), 5UL);
  EXPECT_FALSE(test_map.empty());
  test_map.clear();
  EXPECT_EQ(test_map.size(), 0UL);
  EXPECT_TRUE(test_map.empty());
}

// Test correction of Left-Left Tree imbalance
TEST_F(MapTest, MapLL) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 2; i >= 0; i--) {
    test_map.emplace(kKeys[i], Payload(i));
  }
  EXPECT_EQ(strcmp(Root(&test_map)->pair.first, kKeys[1]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->left->pair.first, kKeys[0]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->right->pair.first, kKeys[2]), 0);
}

// Test correction of Left-Right tree imbalance
TEST_F(MapTest, MapLR) {
  Map<const char*, Payload, StringLess> test_map;
  int insertion_key_index[] = {2, 0, 1};
  for (int i = 0; i < 3; i++) {
    int key_index = insertion_key_index[i];
    test_map.emplace(kKeys[key_index], Payload(key_index));
  }
  EXPECT_EQ(strcmp(Root(&test_map)->pair.first, kKeys[1]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->left->pair.first, kKeys[0]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->right->pair.first, kKeys[2]), 0);
}

// Test correction of Right-Left tree imbalance
TEST_F(MapTest, MapRL) {
  Map<const char*, Payload, StringLess> test_map;
  int insertion_key_index[] = {0, 2, 1};
  for (int i = 0; i < 3; i++) {
    int key_index = insertion_key_index[i];
    test_map.emplace(kKeys[key_index], Payload(key_index));
  }
  EXPECT_EQ(strcmp(Root(&test_map)->pair.first, kKeys[1]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->left->pair.first, kKeys[0]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->right->pair.first, kKeys[2]), 0);
}

// Test correction of Right-Right tree imbalance
TEST_F(MapTest, MapRR) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(kKeys[i], Payload(i));
  }
  EXPECT_EQ(strcmp(Root(&test_map)->pair.first, kKeys[1]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->left->pair.first, kKeys[0]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->right->pair.first, kKeys[3]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->right->left->pair.first, kKeys[2]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->right->right->pair.first, kKeys[4]), 0);
}

// Test correction after random insertion
TEST_F(MapTest, MapRandomInsertions) {
  Map<const char*, Payload, StringLess> test_map;
  int insertion_key_index[] = {1, 4, 3, 0, 2};
  for (int i = 0; i < 5; i++) {
    int key_index = insertion_key_index[i];
    test_map.emplace(kKeys[key_index], Payload(key_index));
  }
  EXPECT_EQ(strcmp(Root(&test_map)->pair.first, kKeys[3]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->left->pair.first, kKeys[1]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->right->pair.first, kKeys[4]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->left->right->pair.first, kKeys[2]), 0);
  EXPECT_EQ(strcmp(Root(&test_map)->left->left->pair.first, kKeys[0]), 0);
}

// Test Map iterator
TEST_F(MapTest, Iteration) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 4; i >= 0; --i) {
    test_map.emplace(kKeys[i], Payload(i));
  }
  auto it = test_map.begin();
  for (int i = 0; i < 5; ++i) {
    ASSERT_NE(it, test_map.end());
    EXPECT_STREQ(kKeys[i], it->first);
    EXPECT_EQ(i, it->second.data());
    ++it;
  }
  EXPECT_EQ(it, test_map.end());
}

// Test Map iterator with unique ptr payload
TEST_F(MapTest, IterationWithUniquePtrValue) {
  Map<const char*, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 4; i >= 0; --i) {
    test_map.emplace(kKeys[i], MakeUnique<Payload>(i));
  }
  auto it = test_map.begin();
  for (int i = 0; i < 5; ++i) {
    ASSERT_NE(it, test_map.end());
    EXPECT_STREQ(kKeys[i], it->first);
    EXPECT_EQ(i, it->second->data());
    ++it;
  }
  EXPECT_EQ(it, test_map.end());
}

// Test Map iterator with unique ptr to char key
TEST_F(MapTest, IterationWithUniquePtrKey) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  for (int i = 4; i >= 0; --i) {
    test_map.emplace(CopyString(kKeys[i]), Payload(i));
  }
  auto it = test_map.begin();
  for (int i = 0; i < 5; ++i) {
    ASSERT_NE(it, test_map.end());
    EXPECT_STREQ(kKeys[i], it->first.get());
    EXPECT_EQ(i, it->second.data());
    ++it;
  }
  EXPECT_EQ(it, test_map.end());
}

// Test removing entries while iterating the map
TEST_F(MapTest, EraseUsingIterator) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(kKeys[i], Payload(i));
  }
  int count = 0;
  for (auto iter = test_map.begin(); iter != test_map.end();) {
    EXPECT_EQ(iter->second.data(), count);
    if (count % 2 == 1) {
      iter = test_map.erase(iter);
    } else {
      ++iter;
    }
    ++count;
  }
  EXPECT_EQ(count, 5);
  auto it = test_map.begin();
  for (int i = 0; i < 5; ++i) {
    if (i % 2 == 0) {
      EXPECT_STREQ(kKeys[i], it->first);
      EXPECT_EQ(i, it->second.data());
      ++it;
    }
  }
  EXPECT_EQ(it, test_map.end());
}

// Random ops on a Map with Integer key of Payload value,
// tests default comparator
TEST_F(MapTest, RandomOpsWithIntKey) {
  Map<int, Payload> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(i, Payload(i));
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map.find(i)->second.data());
  }
  for (int i = 0; i < 5; i++) {
    test_map[i] = Payload(i + 10);
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i + 10, test_map[i].data());
  }
  EXPECT_EQ(test_map.erase(3), 1UL);
  EXPECT_TRUE(test_map.find(3) == test_map.end());
  EXPECT_FALSE(test_map.empty());
  EXPECT_EQ(test_map.size(), 4UL);
  test_map.clear();
  EXPECT_EQ(test_map.size(), 0UL);
  EXPECT_TRUE(test_map.empty());
}

// Tests lower_bound().
TEST_F(MapTest, LowerBound) {
  Map<int, Payload> test_map;
  for (int i = 0; i < 10; i += 2) {
    test_map.emplace(i, Payload(i));
  }
  auto it = test_map.lower_bound(-1);
  EXPECT_EQ(it, test_map.begin());
  it = test_map.lower_bound(0);
  EXPECT_EQ(it, test_map.begin());
  it = test_map.lower_bound(2);
  EXPECT_EQ(it->first, 2);
  it = test_map.lower_bound(3);
  EXPECT_EQ(it->first, 4);
  it = test_map.lower_bound(9);
  EXPECT_EQ(it, test_map.end());
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
