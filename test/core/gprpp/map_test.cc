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

#include <gtest/gtest.h>
#include "src/core/lib/gprpp/map_tester.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
class Payload {
 public:
  Payload(int data) : data_(data) {}
  int data() { return data_; }

 private:
  int data_;
};

struct StringComparator {
  bool operator()(const char* a, const char* b) const {
    return strcmp(a, b) < 0;
  }
};

class ReffedPayload : public ::grpc_core::InternallyRefCounted<ReffedPayload> {
 public:
  ReffedPayload(int data) : data_(data) {}
  int data() { return data_; }
  void Orphan() override {}

 private:
  int data_;
};

class OrphanablePayload
    : public ::grpc_core::InternallyRefCounted<OrphanablePayload> {
 public:
  OrphanablePayload(int data) : data_(data) {}
  int data() { return data_; }
  void Orphan() override { Unref(); }

 private:
  int data_;
};

// Test insertion of raw pointer values
TEST(MapTest, MapAddTestRawPtr) {
  grpc_core::map<const char*, Payload*, StringComparator> test_map;
  Payload* p[3];
  for (int i = 0; i < 3; i++) {
    p[i] = New<Payload>(i);
  }
  test_map.emplace("abc", std::move(p[0]));
  test_map.emplace("efg", std::move(p[1]));
  test_map.emplace("hij", std::move(p[2]));
  EXPECT_EQ(0, test_map.find("abc")->second->data());
  EXPECT_EQ(1, test_map.find("efg")->second->data());
  EXPECT_EQ(2, test_map.find("hij")->second->data());
  for (auto iter = test_map.begin(); iter != test_map.end(); iter++) {
    Delete(iter->second);
  }
  test_map.clear();
}

// Test insertion of raw pointer values
TEST(MapTest, MapTestBracketOperator) {
  grpc_core::map<const char*, Payload*, StringComparator> test_map;
  Payload* p[3];
  for (int i = 0; i < 3; i++) {
    p[i] = New<Payload>(i);
  }
  test_map["abc"] = std::move(p[0]);
  test_map["efg"] = std::move(p[1]);
  test_map["hij"] = std::move(p[2]);
  EXPECT_EQ(0, test_map["abc"]->data());
  EXPECT_EQ(1, test_map["efg"]->data());
  EXPECT_EQ(2, test_map["hij"]->data());
  for (auto iter = test_map.begin(); iter != test_map.end(); iter++) {
    Delete(iter->second);
  }
  test_map.clear();
}

// Test removal of raw pointer values
TEST(MapTest, MapRemoveTestRawPtr) {
  grpc_core::map<const char*, Payload*, StringComparator> test_map;
  Payload* p[3];
  for (int i = 0; i < 3; i++) {
    p[i] = New<Payload>(i);
  }
  test_map.emplace("abc", std::move(p[0]));
  test_map.emplace("efg", std::move(p[1]));
  test_map.emplace("hij", std::move(p[2]));
  Delete(test_map.find("hij")->second);
  test_map.erase("hij");
  EXPECT_EQ(0, test_map.find("abc")->second->data());
  EXPECT_EQ(1, test_map.find("efg")->second->data());
  EXPECT_TRUE(test_map.find("hij") == test_map.end());
  for (auto iter = test_map.begin(); iter != test_map.end(); iter++) {
    Delete(iter->second);
  }
  test_map.clear();
}

// Test insertion of Reffed Pointers
TEST(MapTest, MapAddTestReffedPtr) {
  grpc_core::map<const char*, RefCountedPtr<ReffedPayload>, StringComparator>
      test_map;
  test_map.emplace("abc", MakeRefCounted<ReffedPayload>(1));
  test_map.emplace("efg", MakeRefCounted<ReffedPayload>(2));
  test_map.emplace("hij", MakeRefCounted<ReffedPayload>(3));
  EXPECT_EQ(1, test_map.find("abc")->second->data());
  EXPECT_EQ(2, test_map.find("efg")->second->data());
  EXPECT_EQ(3, test_map.find("hij")->second->data());
  test_map.clear();
}

// Test Removal of Reffed Pointers from map
TEST(MapTest, MapRemoveTestReffedPtr) {
  grpc_core::map<const char*, RefCountedPtr<ReffedPayload>, StringComparator>
      test_map;
  test_map.emplace("xyz", MakeRefCounted<ReffedPayload>(5));
  test_map.emplace("klm", MakeRefCounted<ReffedPayload>(4));
  test_map.emplace("hij", MakeRefCounted<ReffedPayload>(3));
  test_map.emplace("efg", MakeRefCounted<ReffedPayload>(2));
  test_map.emplace("abc", MakeRefCounted<ReffedPayload>(1));
  test_map.erase("hij");
  test_map.erase("abc");
  test_map.erase("klm");
  test_map.erase("xyz");
  test_map.erase("efg");
  EXPECT_TRUE(test_map.find("hij") == test_map.end());
  EXPECT_TRUE(test_map.empty());
}

// Test correction of Left-Left Tree imbalance
TEST(MapTest, MapLL) {
  auto* test_map = New<grpc_core::map<const char*, RefCountedPtr<ReffedPayload>,
                                      StringComparator>>();
  grpc_core::MapTester<const char*, RefCountedPtr<ReffedPayload>,
                       StringComparator>
      mapt(test_map);
  test_map->emplace("hij", MakeRefCounted<ReffedPayload>(3));
  test_map->emplace("efg", MakeRefCounted<ReffedPayload>(2));
  test_map->emplace("abc", MakeRefCounted<ReffedPayload>(1));
  EXPECT_TRUE(!strcmp(mapt.Root()->pair().first, "efg"));
  EXPECT_TRUE(!strcmp(mapt.Left(mapt.Root())->pair().first, "abc"));
  EXPECT_TRUE(!strcmp(mapt.Right(mapt.Root())->pair().first, "hij"));
  EXPECT_EQ(1, test_map->find("abc")->second->data());
  EXPECT_EQ(2, test_map->find("efg")->second->data());
  EXPECT_EQ(3, test_map->find("hij")->second->data());
  test_map->clear();
  Delete(test_map);
}

// Test correction of Left-Right tree imbalance
TEST(MapTest, MapLR) {
  auto* test_map = New<grpc_core::map<const char*, RefCountedPtr<ReffedPayload>,
                                      StringComparator>>();
  grpc_core::MapTester<const char*, RefCountedPtr<ReffedPayload>,
                       StringComparator>
      mapt(test_map);
  test_map->emplace("hij", MakeRefCounted<ReffedPayload>(3));
  test_map->emplace("abc", MakeRefCounted<ReffedPayload>(1));
  test_map->emplace("efg", MakeRefCounted<ReffedPayload>(2));
  EXPECT_TRUE(!strcmp(mapt.Root()->pair().first, "efg"));
  EXPECT_TRUE(!strcmp(mapt.Left(mapt.Root())->pair().first, "abc"));
  EXPECT_TRUE(!strcmp(mapt.Right(mapt.Root())->pair().first, "hij"));
  EXPECT_EQ(1, test_map->find("abc")->second->data());
  EXPECT_EQ(2, test_map->find("efg")->second->data());
  EXPECT_EQ(3, test_map->find("hij")->second->data());
  test_map->clear();
  Delete(test_map);
}

//// Test correction of Right-Left tree imbalance
TEST(MapTest, MapRL) {
  auto* test_map = New<grpc_core::map<const char*, RefCountedPtr<ReffedPayload>,
                                      StringComparator>>();
  grpc_core::MapTester<const char*, RefCountedPtr<ReffedPayload>,
                       StringComparator>
      mapt(test_map);
  test_map->emplace("xyz", MakeRefCounted<ReffedPayload>(5));
  test_map->emplace("klm", MakeRefCounted<ReffedPayload>(4));
  test_map->emplace("hij", MakeRefCounted<ReffedPayload>(3));
  test_map->emplace("efg", MakeRefCounted<ReffedPayload>(2));
  test_map->emplace("abc", MakeRefCounted<ReffedPayload>(1));
  EXPECT_TRUE(!strcmp(mapt.Root()->pair().first, "klm"));
  EXPECT_TRUE(!strcmp(mapt.Left(mapt.Root())->pair().first, "efg"));
  EXPECT_TRUE(!strcmp(mapt.Right(mapt.Root())->pair().first, "xyz"));
  EXPECT_TRUE(!strcmp(mapt.Right(mapt.Left(mapt.Root()))->pair().first, "hij"));
  EXPECT_TRUE(!strcmp(mapt.Left(mapt.Left(mapt.Root()))->pair().first, "abc"));
  EXPECT_EQ(1, test_map->find("abc")->second->data());
  EXPECT_EQ(2, test_map->find("efg")->second->data());
  EXPECT_EQ(3, test_map->find("hij")->second->data());
  EXPECT_EQ(4, test_map->find("klm")->second->data());
  EXPECT_EQ(5, test_map->find("xyz")->second->data());
  test_map->clear();
  Delete(test_map);
}

// Test Map iterator
TEST(MapTest, MapIter) {
  grpc_core::map<const char*, RefCountedPtr<ReffedPayload>, StringComparator>
      test_map;

  test_map.emplace("xyz", MakeRefCounted<ReffedPayload>(5));
  test_map.emplace("klm", MakeRefCounted<ReffedPayload>(4));
  test_map.emplace("hij", MakeRefCounted<ReffedPayload>(3));
  test_map.emplace("efg", MakeRefCounted<ReffedPayload>(2));
  test_map.emplace("abc", MakeRefCounted<ReffedPayload>(1));
  int count = 0;
  for (auto iter = test_map.begin(); iter != test_map.end(); iter++) {
    EXPECT_EQ(iter->second->data(), (count + 1));
    count++;
  }
  EXPECT_EQ(count, 5);
  test_map.clear();
}

// Test removing entries while iterating the map
TEST(MapTest, MapIterAndRemove) {
  grpc_core::map<const char*, RefCountedPtr<ReffedPayload>, StringComparator>
      test_map;

  test_map.emplace("xyz", MakeRefCounted<ReffedPayload>(5));
  test_map.emplace("klm", MakeRefCounted<ReffedPayload>(4));
  test_map.emplace("hij", MakeRefCounted<ReffedPayload>(3));
  test_map.emplace("efg", MakeRefCounted<ReffedPayload>(2));
  test_map.emplace("abc", MakeRefCounted<ReffedPayload>(1));
  int count = 0;
  for (auto iter = test_map.begin(); iter != test_map.end();) {
    EXPECT_EQ(iter->second->data(), (count + 1));
    iter = test_map.erase(iter);
    count++;
  }
  EXPECT_EQ(count, 5);
  EXPECT_TRUE(test_map.empty());
}

// Test insertion of raw pointer values
TEST(MapTest, MapAddTestOrphanablePtr) {
  grpc_core::map<const char*, OrphanablePtr<OrphanablePayload>,
                 StringComparator>
      test_map;
  OrphanablePtr<OrphanablePayload> p[3];
  for (int i = 0; i < 3; i++) {
    p[i] = MakeOrphanable<OrphanablePayload>(i);
  }
  test_map.emplace("abc", std::move(p[0]));
  test_map.emplace("efg", std::move(p[1]));
  test_map.emplace("hij", std::move(p[2]));
  EXPECT_EQ(0, test_map.find("abc")->second->data());
  EXPECT_EQ(1, test_map.find("efg")->second->data());
  EXPECT_EQ(2, test_map.find("hij")->second->data());
  test_map.clear();
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
