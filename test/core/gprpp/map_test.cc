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

const char* const keys[] = {"abc", "efg", "hij", "klm", "xyz"};

class MapTest : public ::testing::Test {
 public:
  template <class Key, class T, class Compare>
  typename ::grpc_core::Map<Key, T, Compare>::Entry* Root(
      typename ::grpc_core::Map<Key, T, Compare>* map) {
    return map->root_;
  }
};

// Test insertion of Payload
TEST_F(MapTest, MapAddTest) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(keys[i], Payload(i));
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map.find(keys[i])->second.data());
    ;
  }
}

// Test insertion of Payload Unique Ptrs
TEST_F(MapTest, MapAddTestWithUniquePtrPayload) {
  Map<const char*, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(keys[i], MakeUnique<Payload>(i));
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map.find(keys[i])->second->data());
    ;
  }
}

// Test insertion of Unique Ptr keys and Payload
TEST_F(MapTest, MapAddTestWithUniquePtrCharAndPayload) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(keys[i]), Payload(i));
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map.find(CopyString(keys[i]))->second.data());
    ;
  }
}

// Test insertion of Unique Ptr keys and Payload
TEST_F(MapTest, MapAddTestWithUniquePtrCharAndUniquePtrPayload) {
  Map<UniquePtr<char>, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(keys[i]), MakeUnique<Payload>(i));
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map.find(CopyString(keys[i]))->second->data());
    ;
  }
}

// Test bracket operators
TEST_F(MapTest, MapTestBracketOperator) {
  Map<int, Payload> test_map;
  for (int i = 0; i < 5; i++) {
    test_map[i] = Payload(i);
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map[i].data());
    ;
  }
}

// Test bracket operators with unique pointer to payload
TEST_F(MapTest, MapTestBracketOperatorWithUniquePtrPayload) {
  Map<int, UniquePtr<Payload>> test_map;
  for (int i = 0; i < 5; i++) {
    test_map[i] = std::move(MakeUnique<Payload>(i));
  }
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(i, test_map[i]->data());
  }
}

// Test removal of a single value
TEST_F(MapTest, MapRemoveTest) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(keys[i], Payload(i));
  }
  test_map.erase(keys[3]);  // Remove "hij"
  for (int i = 0; i < 5; i++) {
    if (i == 3) {  // "hij" should not be present
      EXPECT_TRUE(test_map.find(keys[i]) == test_map.end());
    } else {
      EXPECT_EQ(i, test_map.find(keys[i])->second.data());
    }
  }
}

// Test removal of a single value with unique ptr to payload
TEST_F(MapTest, MapRemoveTestWithUniquePtrPayload) {
  Map<const char*, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(keys[i], MakeUnique<Payload>(i));
  }
  test_map.erase(keys[3]);  // Remove "hij"
  for (int i = 0; i < 5; i++) {
    if (i == 3) {  // "hij" should not be present
      EXPECT_TRUE(test_map.find(keys[i]) == test_map.end());
    } else {
      EXPECT_EQ(i, test_map.find(keys[i])->second->data());
    }
  }
}

// Test removal of a single value
TEST_F(MapTest, MapRemoveTestWithUniquePtrCharAndPayload) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(keys[i]), Payload(i));
  }
  test_map.erase(CopyString(keys[3]));  // Remove "hij"
  for (int i = 0; i < 5; i++) {
    if (i == 3) {  // "hij" should not be present
      EXPECT_TRUE(test_map.find(CopyString(keys[i])) == test_map.end());
    } else {
      EXPECT_EQ(i, test_map.find(CopyString(keys[i]))->second.data());
    }
  }
}

// Test removal of a single value with unique ptr char key and unique ptr
// payload
TEST_F(MapTest, MapRemoveTestWithUniquePtrCharAndUniquePtrPayload) {
  Map<UniquePtr<char>, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(keys[i]), MakeUnique<Payload>(i));
  }
  test_map.erase(CopyString(keys[3]));  // Remove "hij"
  for (int i = 0; i < 5; i++) {
    if (i == 3) {  // "hij" should not be present
      EXPECT_TRUE(test_map.find(CopyString(keys[i])) == test_map.end());
    } else {
      EXPECT_EQ(i, test_map.find(CopyString(keys[i]))->second->data());
    }
  }
}

// Test clear
TEST_F(MapTest, MapClearTest) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(keys[i], Payload(i));
  }
  test_map.clear();
  EXPECT_TRUE(test_map.empty());
}

// Test clear with unique ptr payload
TEST_F(MapTest, MapClearTestWithUniquePtrPayload) {
  Map<const char*, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(keys[i], MakeUnique<Payload>(i));
  }
  test_map.clear();
  EXPECT_TRUE(test_map.empty());
}

// Test clear with unique ptr char key
TEST_F(MapTest, MapClearTestWithUniquePtrChar) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(keys[i]), Payload(i));
  }
  test_map.clear();
  EXPECT_TRUE(test_map.empty());
}

// Test clear with with unique ptr char key and unique ptr to payload
TEST_F(MapTest, MapClearTestWithUniquePtrCharAndUniquePtrPayload) {
  Map<UniquePtr<char>, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(keys[i]), MakeUnique<Payload>(i));
  }
  test_map.clear();
  EXPECT_TRUE(test_map.empty());
}

// Test correction of Left-Left Tree imbalance
TEST_F(MapTest, MapLL) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 2; i >= 0; i--) {
    test_map.emplace(keys[i], Payload(i));
  }
  EXPECT_TRUE(!strcmp(Root(&test_map)->pair.first, keys[1]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->left->pair.first, keys[0]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->right->pair.first, keys[2]));
}

// Test correction of Left-Right tree imbalance
TEST_F(MapTest, MapLR) {
  Map<const char*, Payload, StringLess> test_map;
  int insertion_key_index[] = {2, 0, 1};
  for (int i = 0; i < 3; i++) {
    int key_index = insertion_key_index[i];
    test_map.emplace(keys[key_index], Payload(key_index));
  }
  EXPECT_TRUE(!strcmp(Root(&test_map)->pair.first, keys[1]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->left->pair.first, keys[0]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->right->pair.first, keys[2]));
}

// Test correction of Right-Left tree imbalance
TEST_F(MapTest, MapRL) {
  Map<const char*, Payload, StringLess> test_map;
  int insertion_key_index[] = {0, 2, 1};
  for (int i = 0; i < 3; i++) {
    int key_index = insertion_key_index[i];
    test_map.emplace(keys[key_index], Payload(key_index));
  }
  EXPECT_TRUE(!strcmp(Root(&test_map)->pair.first, keys[1]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->left->pair.first, keys[0]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->right->pair.first, keys[2]));
}

// Test correction of Right-Right tree imbalance
TEST_F(MapTest, MapRR) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(keys[i], Payload(i));
  }
  EXPECT_TRUE(!strcmp(Root(&test_map)->pair.first, keys[1]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->left->pair.first, keys[0]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->right->pair.first, keys[3]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->right->left->pair.first, keys[2]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->right->right->pair.first, keys[4]));
}

// Test correction after random insertion
TEST_F(MapTest, MapRandomInsertions) {
  Map<const char*, Payload, StringLess> test_map;
  int insertion_key_index[] = {1, 4, 3, 0, 2};
  for (int i = 0; i < 5; i++) {
    int key_index = insertion_key_index[i];
    test_map.emplace(keys[key_index], Payload(key_index));
  }
  EXPECT_TRUE(!strcmp(Root(&test_map)->pair.first, keys[3]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->left->pair.first, keys[1]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->right->pair.first, keys[4]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->left->right->pair.first, keys[2]));
  EXPECT_TRUE(!strcmp(Root(&test_map)->left->left->pair.first, keys[0]));
}

// Test Map iterator
TEST_F(MapTest, MapIter) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(keys[i], Payload(i));
  }
  int count = 0;
  for (auto iter = test_map.begin(); iter != test_map.end(); iter++) {
    EXPECT_EQ(iter->second.data(), count);
    count++;
  }
  EXPECT_EQ(count, 5);
}

// Test Map iterator with unique ptr payload
TEST_F(MapTest, MapIterWithUniquePtrPayload) {
  Map<const char*, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(keys[i], MakeUnique<Payload>(i));
  }
  int count = 0;
  for (auto iter = test_map.begin(); iter != test_map.end(); iter++) {
    EXPECT_EQ(iter->second->data(), count);
    count++;
  }
  EXPECT_EQ(count, 5);
}

// Test Map iterator with unique ptr to char key
TEST_F(MapTest, MapIterWithUniquePtrChar) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(keys[i]), Payload(i));
  }
  int count = 0;
  for (auto iter = test_map.begin(); iter != test_map.end(); iter++) {
    EXPECT_EQ(iter->second.data(), count);
    count++;
  }
  EXPECT_EQ(count, 5);
}

// Test Map iterator with unique ptr to char key and unique ptr payload
TEST_F(MapTest, MapIterWithUniquePtrCharAndUniquePtrPayload) {
  Map<UniquePtr<char>, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(keys[i]), MakeUnique<Payload>(i));
  }
  int count = 0;
  for (auto iter = test_map.begin(); iter != test_map.end(); iter++) {
    EXPECT_EQ(iter->second->data(), count);
    count++;
  }
  EXPECT_EQ(count, 5);
}

// Test removing entries while iterating the map
TEST_F(MapTest, MapIterAndRemove) {
  Map<const char*, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(keys[i], Payload(i));
  }
  int count = 0;
  for (auto iter = test_map.begin(); iter != test_map.end();) {
    EXPECT_EQ(iter->second.data(), count);
    iter = test_map.erase(iter);
    count++;
  }
  EXPECT_EQ(count, 5);
  EXPECT_TRUE(test_map.empty());
}

// Test removing entries while iterating the map with unique ptr payload
TEST_F(MapTest, MapIterAndRemoveWithUniquePtrPayload) {
  Map<const char*, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(keys[i], MakeUnique<Payload>(i));
  }
  int count = 0;
  for (auto iter = test_map.begin(); iter != test_map.end();) {
    EXPECT_EQ(iter->second->data(), count);
    iter = test_map.erase(iter);
    count++;
  }
  EXPECT_EQ(count, 5);
  EXPECT_TRUE(test_map.empty());
}

// Test removing entries while iterating the map with unique ptr char key
TEST_F(MapTest, MapIterAndRemoveWithUniquePtrChar) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(keys[i]), Payload(i));
  }
  int count = 0;
  for (auto iter = test_map.begin(); iter != test_map.end();) {
    EXPECT_EQ(iter->second.data(), count);
    iter = test_map.erase(iter);
    count++;
  }
  EXPECT_EQ(count, 5);
  EXPECT_TRUE(test_map.empty());
}

// Test removing entries while iterating the map with unique ptr char key and
// unique ptr payload
TEST_F(MapTest, MapIterAndRemoveWithUniquePtrCharAndUniquePtrPayload) {
  Map<UniquePtr<char>, UniquePtr<Payload>, StringLess> test_map;
  for (int i = 0; i < 5; i++) {
    test_map.emplace(CopyString(keys[i]), MakeUnique<Payload>(i));
  }
  int count = 0;
  for (auto iter = test_map.begin(); iter != test_map.end();) {
    EXPECT_EQ(iter->second->data(), count);
    iter = test_map.erase(iter);
    count++;
  }
  EXPECT_EQ(count, 5);
  EXPECT_TRUE(test_map.empty());
}
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
