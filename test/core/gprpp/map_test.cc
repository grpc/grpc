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
#include <functional>
#include <iostream>
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

class ReffedPayload : public ::grpc_core::InternallyRefCounted<ReffedPayload> {
 public:
  ReffedPayload(int data) : data_(data) {}
  int data() { return data_; }
  void Orphan() override {}

 private:
  int data_;
};

// Test insertion of raw pointer values
TEST(MapTest, MapAddTestRawPtr) {
  grpc_core::Map<const char*, Payload*> map(strcmp);
  Payload* p[3];
  for (int i = 0; i < 3; i++) {
    p[i] = New<Payload>(i);
  }
  map.Insert("abc", p[0]);
  map.Insert("efg", p[1]);
  map.Insert("hij", p[2]);
  EXPECT_EQ(0, map.Find("abc")->data());
  EXPECT_EQ(1, map.Find("efg")->data());
  EXPECT_EQ(2, map.Find("hij")->data());
  for (int i = 0; i < 3; i++) {
    Delete(p[i]);
  }
}

// Test removal of raw pointer values
TEST(MapTest, MapRemoveTestRawPtr) {
  grpc_core::Map<const char*, Payload*> map(strcmp);
  Payload* p[3];
  for (int i = 0; i < 3; i++) {
    p[i] = New<Payload>(i);
  }
  map.Insert("abc", p[0]);
  map.Insert("efg", p[1]);
  map.Insert("hij", p[2]);
  map.Remove("hij");
  EXPECT_EQ(0, map.Find("abc")->data());
  EXPECT_EQ(1, map.Find("efg")->data());
  EXPECT_TRUE(map.Find("hij") == nullptr);
  for (int i = 0; i < 3; i++) {
    Delete(p[i]);
  }
}

// Test insertion of Reffed Pointers
TEST(MapTest, MapAddTestReffedPtr) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>> map(strcmp);
  map.Insert("abc", MakeRefCounted<ReffedPayload>(1));
  map.Insert("efg", MakeRefCounted<ReffedPayload>(2));
  map.Insert("hij", MakeRefCounted<ReffedPayload>(3));
  EXPECT_EQ(1, map.Find("abc")->data());
  EXPECT_EQ(2, map.Find("efg")->data());
  EXPECT_EQ(3, map.Find("hij")->data());
}

// Test Removal of Reffed Pointers from map
TEST(MapTest, MapRemoveTestReffedPtr) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>> map(strcmp);

  map.Insert("xyz", MakeRefCounted<ReffedPayload>(5));
  map.Insert("klm", MakeRefCounted<ReffedPayload>(4));
  map.Insert("hij", MakeRefCounted<ReffedPayload>(3));
  map.Insert("efg", MakeRefCounted<ReffedPayload>(2));
  map.Insert("abc", MakeRefCounted<ReffedPayload>(1));
  map.Remove("hij");
  map.Remove("abc");
  map.Remove("klm");
  map.Remove("xyz");
  map.Remove("efg");
  EXPECT_TRUE(map.Find("hij") == nullptr);
  EXPECT_TRUE(map.Empty());
}

// Test correction of Left-Left Tree imbalance
TEST(MapTest, MapLL) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>> map(strcmp);

  map.Insert("hij", MakeRefCounted<ReffedPayload>(3));
  map.Insert("efg", MakeRefCounted<ReffedPayload>(2));
  map.Insert("abc", MakeRefCounted<ReffedPayload>(1));
  EXPECT_TRUE(!strcmp(map.root()->key(), "efg"));
  EXPECT_TRUE(!strcmp(map.root()->left()->key(), "abc"));
  EXPECT_TRUE(!strcmp(map.root()->right()->key(), "hij"));
}

// Test correction of Left-Right tree imbalance
TEST(MapTest, MapLR) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>> map(strcmp);

  map.Insert("hij", MakeRefCounted<ReffedPayload>(3));
  map.Insert("abc", MakeRefCounted<ReffedPayload>(1));
  map.Insert("efg", MakeRefCounted<ReffedPayload>(2));
  EXPECT_TRUE(!strcmp(map.root()->key(), "efg"));
  EXPECT_TRUE(!strcmp(map.root()->left()->key(), "abc"));
  EXPECT_TRUE(!strcmp(map.root()->right()->key(), "hij"));
}

// Test correction of Right-Left tree imbalance
TEST(MapTest, MapRL) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>> map(strcmp);

  map.Insert("xyz", MakeRefCounted<ReffedPayload>(5));
  map.Insert("klm", MakeRefCounted<ReffedPayload>(4));
  map.Insert("hij", MakeRefCounted<ReffedPayload>(3));
  map.Insert("efg", MakeRefCounted<ReffedPayload>(2));
  map.Insert("abc", MakeRefCounted<ReffedPayload>(1));
  EXPECT_TRUE(!strcmp(map.root()->key(), "klm"));
  EXPECT_TRUE(!strcmp(map.root()->left()->key(), "efg"));
  EXPECT_TRUE(!strcmp(map.root()->left()->left()->key(), "abc"));
  EXPECT_TRUE(!strcmp(map.root()->left()->right()->key(), "hij"));
  EXPECT_TRUE(!strcmp(map.root()->right()->key(), "xyz"));
}

// Test Map iterator
TEST(MapTest, MapIter) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>> map(strcmp);

  map.Insert("xyz", MakeRefCounted<ReffedPayload>(5));
  map.Insert("klm", MakeRefCounted<ReffedPayload>(4));
  map.Insert("hij", MakeRefCounted<ReffedPayload>(3));
  map.Insert("efg", MakeRefCounted<ReffedPayload>(2));
  map.Insert("abc", MakeRefCounted<ReffedPayload>(1));
  int count = 0;
  for (auto iter = map.Begin(); iter != map.End(); iter++)
  {
    EXPECT_EQ(iter.GetValue()->data(), (count + 1));
    count++;
  }
  EXPECT_EQ(count, 5);
}

// Test removing entries while iterating the map
TEST(MapTest, MapIterAndRemove) {
  grpc_core::Map<const char*, RefCountedPtr<ReffedPayload>> map(strcmp);

  map.Insert("xyz", MakeRefCounted<ReffedPayload>(5));
  map.Insert("klm", MakeRefCounted<ReffedPayload>(4));
  map.Insert("hij", MakeRefCounted<ReffedPayload>(3));
  map.Insert("efg", MakeRefCounted<ReffedPayload>(2));
  map.Insert("abc", MakeRefCounted<ReffedPayload>(1));
  int count = 0;
  for (auto iter = map.Begin(); iter != map.End();)
  {
    EXPECT_EQ(iter.RemoveCurrent()->data(), (count + 1));
    count++;
  }
  EXPECT_EQ(count, 5);
  EXPECT_TRUE(map.Empty());
}
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
