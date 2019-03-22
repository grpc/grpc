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
#include "test/core/util/map_tester.h"
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

class MapTest : public ::testing::Test {
 protected:
  MapTest(){};
  MapTester<const char*, RefCountedPtr<ReffedPayload>, StringLess> mapt;
};

// Test insertion of raw pointer values
TEST_F(MapTest, MapAddTest) {
  grpc_core::Map<const char*, Payload*, StringLess> test_map;
  InlinedVector<Payload, 3> p;
  for (int i = 0; i < 3; i++) {
    p.emplace_back(Payload(i));
  }
  test_map.emplace("abc", &p[0]);
  test_map.emplace("efg", &p[1]);
  test_map.emplace("hij", &p[2]);
  EXPECT_EQ(0, test_map.find("abc")->second->data());
  EXPECT_EQ(1, test_map.find("efg")->second->data());
  EXPECT_EQ(2, test_map.find("hij")->second->data());
}

// Test insertion of raw pointer values
TEST_F(MapTest, MapTestBracketOperator) {
  grpc_core::Map<const char*, Payload*, StringLess> test_map;
  InlinedVector<Payload, 3> p;
  for (int i = 0; i < 3; i++) {
    p.emplace_back(Payload(i));
  }
  test_map["abc"] = &p[0];
  test_map["efg"] = &p[1];
  test_map["hij"] = &p[2];
  EXPECT_EQ(0, test_map["abc"]->data());
  EXPECT_EQ(1, test_map["efg"]->data());
  EXPECT_EQ(2, test_map["hij"]->data());
}

// Test removal of raw pointer values
TEST_F(MapTest, MapRemoveTestRawPtr) {
  grpc_core::Map<const char*, Payload*, StringLess> test_map;
  InlinedVector<Payload, 3> p;
  for (int i = 0; i < 3; i++) {
    p.emplace_back(Payload(i));
  }
  test_map.emplace("abc", &p[0]);
  test_map.emplace("efg", &p[1]);
  test_map.emplace("hij", &p[2]);
  test_map.erase("hij");
  EXPECT_EQ(0, test_map.find("abc")->second->data());
  EXPECT_EQ(1, test_map.find("efg")->second->data());
  EXPECT_TRUE(test_map.find("hij") == test_map.end());
}

// Test insertion of Reffed Pointers
TEST_F(MapTest, MapAddTestReffedPtr) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  test_map.emplace("abc", MakeRefCounted<ReffedPayload>(1));
  test_map.emplace("efg", MakeRefCounted<ReffedPayload>(2));
  test_map.emplace("hij", MakeRefCounted<ReffedPayload>(3));
  EXPECT_EQ(1, test_map.find("abc")->second->data());
  EXPECT_EQ(2, test_map.find("efg")->second->data());
  EXPECT_EQ(3, test_map.find("hij")->second->data());
}

// Test clear
TEST_F(MapTest, MapClearTestReffedPtr) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  test_map.emplace("abc", MakeRefCounted<ReffedPayload>(1));
  test_map.emplace("efg", MakeRefCounted<ReffedPayload>(2));
  test_map.emplace("hij", MakeRefCounted<ReffedPayload>(3));
  test_map.clear();
  EXPECT_TRUE(test_map.empty());
}

// Test Removal of Reffed Pointers from map
TEST_F(MapTest, MapRemoveTestReffedPtr) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>, StringLess>
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
TEST_F(MapTest, MapLL) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  test_map.emplace("hij", MakeRefCounted<ReffedPayload>(3));
  test_map.emplace("efg", MakeRefCounted<ReffedPayload>(2));
  test_map.emplace("abc", MakeRefCounted<ReffedPayload>(1));
  EXPECT_TRUE(!strcmp(mapt.Root(&test_map)->pair.first, "efg"));
  EXPECT_TRUE(!strcmp(mapt.Left(mapt.Root(&test_map))->pair.first, "abc"));
  EXPECT_TRUE(!strcmp(mapt.Right(mapt.Root(&test_map))->pair.first, "hij"));
  EXPECT_EQ(1, test_map.find("abc")->second->data());
  EXPECT_EQ(2, test_map.find("efg")->second->data());
  EXPECT_EQ(3, test_map.find("hij")->second->data());
}

// Test correction of Left-Right tree imbalance
TEST_F(MapTest, MapLR) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  test_map.emplace("hij", MakeRefCounted<ReffedPayload>(3));
  test_map.emplace("abc", MakeRefCounted<ReffedPayload>(1));
  test_map.emplace("efg", MakeRefCounted<ReffedPayload>(2));
  EXPECT_TRUE(!strcmp(mapt.Root(&test_map)->pair.first, "efg"));
  EXPECT_TRUE(!strcmp(mapt.Left(mapt.Root(&test_map))->pair.first, "abc"));
  EXPECT_TRUE(!strcmp(mapt.Right(mapt.Root(&test_map))->pair.first, "hij"));
  EXPECT_EQ(1, test_map.find("abc")->second->data());
  EXPECT_EQ(2, test_map.find("efg")->second->data());
  EXPECT_EQ(3, test_map.find("hij")->second->data());
}

// Test correction of Right-Left tree imbalance
TEST_F(MapTest, MapRL) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  test_map.emplace("xyz", MakeRefCounted<ReffedPayload>(5));
  test_map.emplace("klm", MakeRefCounted<ReffedPayload>(4));
  test_map.emplace("hij", MakeRefCounted<ReffedPayload>(3));
  test_map.emplace("efg", MakeRefCounted<ReffedPayload>(2));
  test_map.emplace("abc", MakeRefCounted<ReffedPayload>(1));
  EXPECT_TRUE(!strcmp(mapt.Root(&test_map)->pair.first, "klm"));
  EXPECT_TRUE(!strcmp(mapt.Left(mapt.Root(&test_map))->pair.first, "efg"));
  EXPECT_TRUE(!strcmp(mapt.Right(mapt.Root(&test_map))->pair.first, "xyz"));
  EXPECT_TRUE(
      !strcmp(mapt.Right(mapt.Left(mapt.Root(&test_map)))->pair.first, "hij"));
  EXPECT_TRUE(
      !strcmp(mapt.Left(mapt.Left(mapt.Root(&test_map)))->pair.first, "abc"));
  EXPECT_EQ(1, test_map.find("abc")->second->data());
  EXPECT_EQ(2, test_map.find("efg")->second->data());
  EXPECT_EQ(3, test_map.find("hij")->second->data());
  EXPECT_EQ(4, test_map.find("klm")->second->data());
  EXPECT_EQ(5, test_map.find("xyz")->second->data());
}

// Test correction of Right-Right tree imbalance
TEST_F(MapTest, MapRR) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  test_map.emplace("abc", MakeRefCounted<ReffedPayload>(1));
  test_map.emplace("efg", MakeRefCounted<ReffedPayload>(2));
  test_map.emplace("hij", MakeRefCounted<ReffedPayload>(3));
  test_map.emplace("klm", MakeRefCounted<ReffedPayload>(4));
  test_map.emplace("xyz", MakeRefCounted<ReffedPayload>(5));
  EXPECT_TRUE(!strcmp(mapt.Root(&test_map)->pair.first, "efg"));
  EXPECT_TRUE(!strcmp(mapt.Left(mapt.Root(&test_map))->pair.first, "abc"));
  EXPECT_TRUE(!strcmp(mapt.Right(mapt.Root(&test_map))->pair.first, "klm"));
  EXPECT_TRUE(
      !strcmp(mapt.Left(mapt.Right(mapt.Root(&test_map)))->pair.first, "hij"));
  EXPECT_TRUE(
      !strcmp(mapt.Right(mapt.Right(mapt.Root(&test_map)))->pair.first, "xyz"));
  EXPECT_EQ(1, test_map.find("abc")->second->data());
  EXPECT_EQ(2, test_map.find("efg")->second->data());
  EXPECT_EQ(3, test_map.find("hij")->second->data());
  EXPECT_EQ(4, test_map.find("klm")->second->data());
  EXPECT_EQ(5, test_map.find("xyz")->second->data());
}

// Test correction after random insertion
TEST_F(MapTest, MapRandomInsertions) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  test_map.emplace("efg", MakeRefCounted<ReffedPayload>(2));
  test_map.emplace("xyz", MakeRefCounted<ReffedPayload>(5));
  test_map.emplace("klm", MakeRefCounted<ReffedPayload>(4));
  test_map.emplace("abc", MakeRefCounted<ReffedPayload>(1));
  test_map.emplace("hij", MakeRefCounted<ReffedPayload>(3));
  EXPECT_TRUE(!strcmp(mapt.Root(&test_map)->pair.first, "klm"));
  EXPECT_TRUE(!strcmp(mapt.Left(mapt.Root(&test_map))->pair.first, "efg"));
  EXPECT_TRUE(!strcmp(mapt.Right(mapt.Root(&test_map))->pair.first, "xyz"));
  EXPECT_TRUE(
      !strcmp(mapt.Right(mapt.Left(mapt.Root(&test_map)))->pair.first, "hij"));
  EXPECT_TRUE(
      !strcmp(mapt.Left(mapt.Left(mapt.Root(&test_map)))->pair.first, "abc"));
  EXPECT_EQ(1, test_map.find("abc")->second->data());
  EXPECT_EQ(2, test_map.find("efg")->second->data());
  EXPECT_EQ(3, test_map.find("hij")->second->data());
  EXPECT_EQ(4, test_map.find("klm")->second->data());
  EXPECT_EQ(5, test_map.find("xyz")->second->data());
}

// Test Map iterator
TEST_F(MapTest, MapIter) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>, StringLess>
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
}

// Test removing entries while iterating the map
TEST_F(MapTest, MapIterAndRemove) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>, StringLess>
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
// TEST_F(MapTest, MapAddTestOrphanablePtr) {
//  grpc_core::Map<const char*, OrphanablePtr<OrphanablePayload>, StringLess>
//      test_map;
//  OrphanablePtr<OrphanablePayload> p[3];
//  for (int i = 0; i < 3; i++) {
//    p[i] = MakeOrphanable<OrphanablePayload>(i);
//  }
//  test_map.emplace("abc", std::move(p[0]));
//  test_map.emplace("efg", std::move(p[1]));
//  test_map.emplace("hij", std::move(p[2]));
//  EXPECT_EQ(0, test_map.find("abc")->second->data());
//  EXPECT_EQ(1, test_map.find("efg")->second->data());
//  EXPECT_EQ(2, test_map.find("hij")->second->data());
//}
//
//// Test insertion of unique pointer values
// TEST_F(MapTest, MapAddTestUniquePtr) {
//  grpc_core::Map<const char*, UniquePtr<Payload>, StringLess> test_map;
//  UniquePtr<Payload> p[3];
//  for (int i = 0; i < 3; i++) {
//    p[i] = MakeUnique<Payload>(i);
//  }
//  test_map.emplace("abc", std::move(p[0]));
//  test_map.emplace("efg", std::move(p[1]));
//  test_map.emplace("hij", std::move(p[2]));
//  EXPECT_EQ(0, test_map.find("abc")->second->data());
//  EXPECT_EQ(1, test_map.find("efg")->second->data());
//  EXPECT_EQ(2, test_map.find("hij")->second->data());
//}
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
