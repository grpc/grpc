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
  Payload() : data_(-1) {}
  explicit Payload(int data) : data_(data) {}
  int data() { return data_; }

 private:
  int data_;
};

class ReffedPayload : public InternallyRefCounted<ReffedPayload> {
 public:
  explicit ReffedPayload(int data) : data_(data) {}
  int data() { return data_; }
  void Orphan() override { Unref(); }

 private:
  int data_;
};

class OrphanablePayload : public InternallyRefCounted<OrphanablePayload> {
 public:
  explicit OrphanablePayload(int data) : data_(data) {}
  int data() { return data_; }
  void Orphan() override { Unref(); }

 private:
  int data_;
};

inline UniquePtr<char> CopyString(const char* string) {
  return UniquePtr<char>(gpr_strdup(string));
}

class MapTest : public ::testing::Test {
 protected:
  MapTest() {
    abc_ = CopyString("abc");
    efg_ = CopyString("efg");
    hij_ = CopyString("hij");
    klm_ = CopyString("klm");
    xyz_ = CopyString("xyz");
  };
  MapTester<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess> mapt_;
  UniquePtr<char> abc_, efg_, hij_, klm_, xyz_;
};

// Test insertion of raw pointer values
TEST_F(MapTest, MapAddTest) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  test_map.emplace(std::move(abc_copy), Payload(0));
  test_map.emplace(std::move(efg_copy), Payload(1));
  test_map.emplace(std::move(hij_copy), Payload(2));
  EXPECT_EQ(0, test_map.find(abc_)->second.data());
  EXPECT_EQ(1, test_map.find(efg_)->second.data());
  EXPECT_EQ(2, test_map.find(hij_)->second.data());
}

TEST_F(MapTest, MapConstCharAddTest) {
  Map<const char*, Payload, StringLess> test_map;
  test_map.emplace("abc", Payload(0));
  test_map.emplace("efg", Payload(1));
  test_map.emplace("hij", Payload(2));
  EXPECT_EQ(0, test_map.find("abc")->second.data());
  EXPECT_EQ(1, test_map.find("efg")->second.data());
  EXPECT_EQ(2, test_map.find("hij")->second.data());
}

// Test insertion of raw pointer values
TEST_F(MapTest, MapTestBracketOperator) {
  Map<int, Payload> test_map;
  test_map[0] = Payload(0);
  test_map[1] = Payload(1);
  test_map[2] = Payload(2);
  EXPECT_EQ(0, test_map[0].data());
  EXPECT_EQ(1, test_map[1].data());
  EXPECT_EQ(2, test_map[2].data());
}

TEST_F(MapTest, MapConstCharReffedPayloadAddTest) {
  Map<const char*, RefCountedPtr<ReffedPayload>, StringLess> test_map;
  test_map.emplace("abc", MakeRefCounted<ReffedPayload>(0));
  test_map.emplace("efg", MakeRefCounted<ReffedPayload>(1));
  test_map.emplace("hij", MakeRefCounted<ReffedPayload>(2));
  EXPECT_EQ(0, test_map.find("abc")->second->data());
  EXPECT_EQ(1, test_map.find("efg")->second->data());
  EXPECT_EQ(2, test_map.find("hij")->second->data());
}

// Test removal of raw pointer values
TEST_F(MapTest, MapRemoveTestRawPtr) {
  Map<UniquePtr<char>, Payload, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  test_map.emplace(std::move(abc_copy), Payload(0));
  test_map.emplace(std::move(efg_copy), Payload(1));
  test_map.emplace(std::move(hij_copy), Payload(2));
  test_map.erase(hij_);
  EXPECT_EQ(0, test_map.find(abc_)->second.data());
  EXPECT_EQ(1, test_map.find(efg_)->second.data());
  EXPECT_TRUE(test_map.find(hij_) == test_map.end());
}

// Test insertion of Reffed Pointers
TEST_F(MapTest, MapAddTestReffedPtr) {
  Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  EXPECT_EQ(1, test_map.find(abc_)->second->data());
  EXPECT_EQ(2, test_map.find(efg_)->second->data());
  EXPECT_EQ(3, test_map.find(hij_)->second->data());
}

// Test clear
TEST_F(MapTest, MapClearTestReffedPtr) {
  Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.clear();
  EXPECT_TRUE(test_map.empty());
}

// Test Removal of Reffed Pointers from map
TEST_F(MapTest, MapRemoveTestReffedPtr) {
  Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  UniquePtr<char> klm_copy = CopyString(klm_.get());
  UniquePtr<char> xyz_copy = CopyString(xyz_.get());
  test_map.emplace(std::move(xyz_copy), MakeRefCounted<ReffedPayload>(5));
  test_map.emplace(std::move(klm_copy), MakeRefCounted<ReffedPayload>(4));
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.erase(hij_);
  test_map.erase(abc_);
  test_map.erase(klm_);
  test_map.erase(xyz_);
  test_map.erase(efg_);
  EXPECT_TRUE(test_map.find(hij_) == test_map.end());
  EXPECT_TRUE(test_map.empty());
}

// Test correction of Left-Left Tree imbalance
TEST_F(MapTest, MapLL) {
  Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  EXPECT_TRUE(!strcmp(mapt_.Root(&test_map)->pair.first.get(), efg_.get()));
  EXPECT_TRUE(
      !strcmp(mapt_.Left(mapt_.Root(&test_map))->pair.first.get(), abc_.get()));
  EXPECT_TRUE(!strcmp(mapt_.Right(mapt_.Root(&test_map))->pair.first.get(),
                      hij_.get()));
  EXPECT_EQ(1, test_map.find(abc_)->second->data());
  EXPECT_EQ(2, test_map.find(efg_)->second->data());
  EXPECT_EQ(3, test_map.find(hij_)->second->data());
}

// Test correction of Left-Right tree imbalance
TEST_F(MapTest, MapLR) {
  Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  EXPECT_TRUE(!strcmp(mapt_.Root(&test_map)->pair.first.get(), efg_.get()));
  EXPECT_TRUE(
      !strcmp(mapt_.Left(mapt_.Root(&test_map))->pair.first.get(), abc_.get()));
  EXPECT_TRUE(!strcmp(mapt_.Right(mapt_.Root(&test_map))->pair.first.get(),
                      hij_.get()));
  EXPECT_EQ(1, test_map.find(abc_)->second->data());
  EXPECT_EQ(2, test_map.find(efg_)->second->data());
  EXPECT_EQ(3, test_map.find(hij_)->second->data());
}

// Test correction of Right-Left tree imbalance
TEST_F(MapTest, MapRL) {
  Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  UniquePtr<char> klm_copy = CopyString(klm_.get());
  UniquePtr<char> xyz_copy = CopyString(xyz_.get());
  test_map.emplace(std::move(xyz_copy), MakeRefCounted<ReffedPayload>(5));
  test_map.emplace(std::move(klm_copy), MakeRefCounted<ReffedPayload>(4));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  EXPECT_TRUE(!strcmp(mapt_.Root(&test_map)->pair.first.get(), klm_.get()));
  EXPECT_TRUE(
      !strcmp(mapt_.Left(mapt_.Root(&test_map))->pair.first.get(), efg_.get()));
  EXPECT_TRUE(!strcmp(mapt_.Right(mapt_.Root(&test_map))->pair.first.get(),
                      xyz_.get()));
  EXPECT_TRUE(
      !strcmp(mapt_.Right(mapt_.Left(mapt_.Root(&test_map)))->pair.first.get(),
              hij_.get()));
  EXPECT_TRUE(
      !strcmp(mapt_.Left(mapt_.Left(mapt_.Root(&test_map)))->pair.first.get(),
              abc_.get()));
  EXPECT_EQ(1, test_map.find(abc_)->second->data());
  EXPECT_EQ(2, test_map.find(efg_)->second->data());
  EXPECT_EQ(3, test_map.find(hij_)->second->data());
  EXPECT_EQ(4, test_map.find(klm_)->second->data());
  EXPECT_EQ(5, test_map.find(xyz_)->second->data());
}

// Test correction of Right-Right tree imbalance
TEST_F(MapTest, MapRR) {
  Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  UniquePtr<char> klm_copy = CopyString(klm_.get());
  UniquePtr<char> xyz_copy = CopyString(xyz_.get());
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.emplace(std::move(klm_copy), MakeRefCounted<ReffedPayload>(4));
  test_map.emplace(std::move(xyz_copy), MakeRefCounted<ReffedPayload>(5));
  EXPECT_TRUE(!strcmp(mapt_.Root(&test_map)->pair.first.get(), efg_.get()));
  EXPECT_TRUE(
      !strcmp(mapt_.Left(mapt_.Root(&test_map))->pair.first.get(), abc_.get()));
  EXPECT_TRUE(!strcmp(mapt_.Right(mapt_.Root(&test_map))->pair.first.get(),
                      klm_.get()));
  EXPECT_TRUE(
      !strcmp(mapt_.Left(mapt_.Right(mapt_.Root(&test_map)))->pair.first.get(),
              hij_.get()));
  EXPECT_TRUE(
      !strcmp(mapt_.Right(mapt_.Right(mapt_.Root(&test_map)))->pair.first.get(),
              xyz_.get()));
  EXPECT_EQ(1, test_map.find(abc_)->second->data());
  EXPECT_EQ(2, test_map.find(efg_)->second->data());
  EXPECT_EQ(3, test_map.find(hij_)->second->data());
  EXPECT_EQ(4, test_map.find(klm_)->second->data());
  EXPECT_EQ(5, test_map.find(xyz_)->second->data());
}

// Test correction after random insertion
TEST_F(MapTest, MapRandomInsertions) {
  Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  UniquePtr<char> klm_copy = CopyString(klm_.get());
  UniquePtr<char> xyz_copy = CopyString(xyz_.get());
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(xyz_copy), MakeRefCounted<ReffedPayload>(5));
  test_map.emplace(std::move(klm_copy), MakeRefCounted<ReffedPayload>(4));
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  EXPECT_TRUE(!strcmp(mapt_.Root(&test_map)->pair.first.get(), klm_.get()));
  EXPECT_TRUE(
      !strcmp(mapt_.Left(mapt_.Root(&test_map))->pair.first.get(), efg_.get()));
  EXPECT_TRUE(!strcmp(mapt_.Right(mapt_.Root(&test_map))->pair.first.get(),
                      xyz_.get()));
  EXPECT_TRUE(
      !strcmp(mapt_.Right(mapt_.Left(mapt_.Root(&test_map)))->pair.first.get(),
              hij_.get()));
  EXPECT_TRUE(
      !strcmp(mapt_.Left(mapt_.Left(mapt_.Root(&test_map)))->pair.first.get(),
              abc_.get()));
  EXPECT_EQ(1, test_map.find(abc_)->second->data());
  EXPECT_EQ(2, test_map.find(efg_)->second->data());
  EXPECT_EQ(3, test_map.find(hij_)->second->data());
  EXPECT_EQ(4, test_map.find(klm_)->second->data());
  EXPECT_EQ(5, test_map.find(xyz_)->second->data());
}

// Test Map iterator
TEST_F(MapTest, MapIter) {
  Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  UniquePtr<char> klm_copy = CopyString(klm_.get());
  UniquePtr<char> xyz_copy = CopyString(xyz_.get());
  test_map.emplace(std::move(xyz_copy), MakeRefCounted<ReffedPayload>(5));
  test_map.emplace(std::move(klm_copy), MakeRefCounted<ReffedPayload>(4));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  int count = 0;
  for (auto iter = test_map.begin(); iter != test_map.end(); iter++) {
    EXPECT_EQ(iter->second->data(), (count + 1));
    count++;
  }
  EXPECT_EQ(count, 5);
}

// Test removing entries while iterating the map
TEST_F(MapTest, MapIterAndRemove) {
  Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  UniquePtr<char> klm_copy = CopyString(klm_.get());
  UniquePtr<char> xyz_copy = CopyString(xyz_.get());
  test_map.emplace(std::move(xyz_copy), MakeRefCounted<ReffedPayload>(5));
  test_map.emplace(std::move(klm_copy), MakeRefCounted<ReffedPayload>(4));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
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
TEST_F(MapTest, MapAddTestOrphanablePtr) {
  Map<UniquePtr<char>, OrphanablePtr<OrphanablePayload>, StringLess> test_map;
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  test_map.emplace(std::move(abc_copy), MakeOrphanable<OrphanablePayload>(0));
  test_map.emplace(std::move(efg_copy), MakeOrphanable<OrphanablePayload>(1));
  test_map.emplace(std::move(hij_copy), MakeOrphanable<OrphanablePayload>(2));
  EXPECT_EQ(0, test_map.find(abc_)->second->data());
  EXPECT_EQ(1, test_map.find(efg_)->second->data());
  EXPECT_EQ(2, test_map.find(hij_)->second->data());
}

// Test insertion of unique pointer values
TEST_F(MapTest, MapAddTestUniquePtr) {
  Map<UniquePtr<char>, UniquePtr<Payload>, StringLess> test_map;
  UniquePtr<Payload> p[3];
  for (int i = 0; i < 3; i++) {
    p[i] = MakeUnique<Payload>(i);
  }
  UniquePtr<char> abc_copy = CopyString(abc_.get());
  UniquePtr<char> efg_copy = CopyString(efg_.get());
  UniquePtr<char> hij_copy = CopyString(hij_.get());
  test_map.emplace(std::move(abc_copy), std::move(p[0]));
  test_map.emplace(std::move(efg_copy), std::move(p[1]));
  test_map.emplace(std::move(hij_copy), std::move(p[2]));
  EXPECT_EQ(0, test_map.find(abc_)->second->data());
  EXPECT_EQ(1, test_map.find(efg_)->second->data());
  EXPECT_EQ(2, test_map.find(hij_)->second->data());
}
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
