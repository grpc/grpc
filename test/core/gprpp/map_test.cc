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
  explicit Payload(int data) : data_(data) {}
  int data() { return data_; }

 private:
  int data_;
};

class ReffedPayload : public ::grpc_core::InternallyRefCounted<ReffedPayload> {
 public:
  explicit ReffedPayload(int data) : data_(data) {}
  int data() { return data_; }
  void Orphan() override {}

 private:
  int data_;
};

class OrphanablePayload
    : public ::grpc_core::InternallyRefCounted<OrphanablePayload> {
 public:
  explicit OrphanablePayload(int data) : data_(data) {}
  int data() { return data_; }
  void Orphan() override { Unref(); }

 private:
  int data_;
};

inline UniquePtr<char> getStringCopy(const char* string) {
  return UniquePtr<char>(gpr_strdup(string));
}

class MapTest : public ::testing::Test {
 protected:
  MapTest() {
    abc = getStringCopy("abc");
    efg = getStringCopy("efg");
    hij = getStringCopy("hij");
    klm = getStringCopy("klm");
    xyz = getStringCopy("xyz");
  };
  MapTester<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess> mapt;
  UniquePtr<char> abc, efg, hij, klm, xyz;
};

// Test insertion of raw pointer values
TEST_F(MapTest, MapAddTest) {
  grpc_core::Map<UniquePtr<char>, Payload*, StringLess> test_map;
  InlinedVector<Payload, 3> p;
  for (int i = 0; i < 3; i++) {
    p.emplace_back(Payload(i));
  }

  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  test_map.emplace(std::move(abc_copy), &p[0]);
  test_map.emplace(std::move(efg_copy), &p[1]);
  test_map.emplace(std::move(hij_copy), &p[2]);
  EXPECT_EQ(0, test_map.find(abc)->second->data());
  EXPECT_EQ(1, test_map.find(efg)->second->data());
  EXPECT_EQ(2, test_map.find(hij)->second->data());
}

// Test insertion of raw pointer values
TEST_F(MapTest, MapTestBracketOperator) {
  grpc_core::Map<int, Payload*> test_map;
  InlinedVector<Payload, 3> p;
  for (int i = 0; i < 3; i++) {
    p.emplace_back(Payload(i));
  }
  test_map[0] = &p[0];
  test_map[1] = &p[1];
  test_map[2] = &p[2];
  EXPECT_EQ(0, test_map[0]->data());
  EXPECT_EQ(1, test_map[1]->data());
  EXPECT_EQ(2, test_map[2]->data());
}

// Test removal of raw pointer values
TEST_F(MapTest, MapRemoveTestRawPtr) {
  grpc_core::Map<UniquePtr<char>, Payload*, StringLess> test_map;
  InlinedVector<Payload, 3> p;
  for (int i = 0; i < 3; i++) {
    p.emplace_back(Payload(i));
  }
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  test_map.emplace(std::move(abc_copy), &p[0]);
  test_map.emplace(std::move(efg_copy), &p[1]);
  test_map.emplace(std::move(hij_copy), &p[2]);
  test_map.erase(hij);
  EXPECT_EQ(0, test_map.find(abc)->second->data());
  EXPECT_EQ(1, test_map.find(efg)->second->data());
  EXPECT_TRUE(test_map.find(hij) == test_map.end());
}

// Test insertion of Reffed Pointers
TEST_F(MapTest, MapAddTestReffedPtr) {
  grpc_core::Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  EXPECT_EQ(1, test_map.find(abc)->second->data());
  EXPECT_EQ(2, test_map.find(efg)->second->data());
  EXPECT_EQ(3, test_map.find(hij)->second->data());
}

// Test clear
TEST_F(MapTest, MapClearTestReffedPtr) {
  grpc_core::Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.clear();
  EXPECT_TRUE(test_map.empty());
}

// Test Removal of Reffed Pointers from map
TEST_F(MapTest, MapRemoveTestReffedPtr) {
  grpc_core::Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  UniquePtr<char> klm_copy = getStringCopy(klm.get());
  UniquePtr<char> xyz_copy = getStringCopy(xyz.get());
  test_map.emplace(std::move(xyz_copy), MakeRefCounted<ReffedPayload>(5));
  test_map.emplace(std::move(klm_copy), MakeRefCounted<ReffedPayload>(4));
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.erase(hij);
  test_map.erase(abc);
  test_map.erase(klm);
  test_map.erase(xyz);
  test_map.erase(efg);
  EXPECT_TRUE(test_map.find(hij) == test_map.end());
  EXPECT_TRUE(test_map.empty());
}

// Test correction of Left-Left Tree imbalance
TEST_F(MapTest, MapLL) {
  grpc_core::Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  EXPECT_TRUE(!strcmp(mapt.Root(&test_map)->pair.first.get(), efg.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Left(mapt.Root(&test_map))->pair.first.get(), abc.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Right(mapt.Root(&test_map))->pair.first.get(), hij.get()));
  EXPECT_EQ(1, test_map.find(abc)->second->data());
  EXPECT_EQ(2, test_map.find(efg)->second->data());
  EXPECT_EQ(3, test_map.find(hij)->second->data());
}

// Test correction of Left-Right tree imbalance
TEST_F(MapTest, MapLR) {
  grpc_core::Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  EXPECT_TRUE(!strcmp(mapt.Root(&test_map)->pair.first.get(), efg.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Left(mapt.Root(&test_map))->pair.first.get(), abc.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Right(mapt.Root(&test_map))->pair.first.get(), hij.get()));
  EXPECT_EQ(1, test_map.find(abc)->second->data());
  EXPECT_EQ(2, test_map.find(efg)->second->data());
  EXPECT_EQ(3, test_map.find(hij)->second->data());
}

// Test correction of Right-Left tree imbalance
TEST_F(MapTest, MapRL) {
  grpc_core::Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  UniquePtr<char> klm_copy = getStringCopy(klm.get());
  UniquePtr<char> xyz_copy = getStringCopy(xyz.get());
  test_map.emplace(std::move(xyz_copy), MakeRefCounted<ReffedPayload>(5));
  test_map.emplace(std::move(klm_copy), MakeRefCounted<ReffedPayload>(4));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  EXPECT_TRUE(!strcmp(mapt.Root(&test_map)->pair.first.get(), klm.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Left(mapt.Root(&test_map))->pair.first.get(), efg.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Right(mapt.Root(&test_map))->pair.first.get(), xyz.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Right(mapt.Left(mapt.Root(&test_map)))->pair.first.get(),
              hij.get()));
  EXPECT_TRUE(!strcmp(
      mapt.Left(mapt.Left(mapt.Root(&test_map)))->pair.first.get(), abc.get()));
  EXPECT_EQ(1, test_map.find(abc)->second->data());
  EXPECT_EQ(2, test_map.find(efg)->second->data());
  EXPECT_EQ(3, test_map.find(hij)->second->data());
  EXPECT_EQ(4, test_map.find(klm)->second->data());
  EXPECT_EQ(5, test_map.find(xyz)->second->data());
}

// Test correction of Right-Right tree imbalance
TEST_F(MapTest, MapRR) {
  grpc_core::Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  UniquePtr<char> klm_copy = getStringCopy(klm.get());
  UniquePtr<char> xyz_copy = getStringCopy(xyz.get());
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  test_map.emplace(std::move(klm_copy), MakeRefCounted<ReffedPayload>(4));
  test_map.emplace(std::move(xyz_copy), MakeRefCounted<ReffedPayload>(5));
  EXPECT_TRUE(!strcmp(mapt.Root(&test_map)->pair.first.get(), efg.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Left(mapt.Root(&test_map))->pair.first.get(), abc.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Right(mapt.Root(&test_map))->pair.first.get(), klm.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Left(mapt.Right(mapt.Root(&test_map)))->pair.first.get(),
              hij.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Right(mapt.Right(mapt.Root(&test_map)))->pair.first.get(),
              xyz.get()));
  EXPECT_EQ(1, test_map.find(abc)->second->data());
  EXPECT_EQ(2, test_map.find(efg)->second->data());
  EXPECT_EQ(3, test_map.find(hij)->second->data());
  EXPECT_EQ(4, test_map.find(klm)->second->data());
  EXPECT_EQ(5, test_map.find(xyz)->second->data());
}

// Test correction after random insertion
TEST_F(MapTest, MapRandomInsertions) {
  grpc_core::Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  UniquePtr<char> klm_copy = getStringCopy(klm.get());
  UniquePtr<char> xyz_copy = getStringCopy(xyz.get());
  test_map.emplace(std::move(efg_copy), MakeRefCounted<ReffedPayload>(2));
  test_map.emplace(std::move(xyz_copy), MakeRefCounted<ReffedPayload>(5));
  test_map.emplace(std::move(klm_copy), MakeRefCounted<ReffedPayload>(4));
  test_map.emplace(std::move(abc_copy), MakeRefCounted<ReffedPayload>(1));
  test_map.emplace(std::move(hij_copy), MakeRefCounted<ReffedPayload>(3));
  EXPECT_TRUE(!strcmp(mapt.Root(&test_map)->pair.first.get(), klm.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Left(mapt.Root(&test_map))->pair.first.get(), efg.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Right(mapt.Root(&test_map))->pair.first.get(), xyz.get()));
  EXPECT_TRUE(
      !strcmp(mapt.Right(mapt.Left(mapt.Root(&test_map)))->pair.first.get(),
              hij.get()));
  EXPECT_TRUE(!strcmp(
      mapt.Left(mapt.Left(mapt.Root(&test_map)))->pair.first.get(), abc.get()));
  EXPECT_EQ(1, test_map.find(abc)->second->data());
  EXPECT_EQ(2, test_map.find(efg)->second->data());
  EXPECT_EQ(3, test_map.find(hij)->second->data());
  EXPECT_EQ(4, test_map.find(klm)->second->data());
  EXPECT_EQ(5, test_map.find(xyz)->second->data());
}

// Test Map iterator
TEST_F(MapTest, MapIter) {
  grpc_core::Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  UniquePtr<char> klm_copy = getStringCopy(klm.get());
  UniquePtr<char> xyz_copy = getStringCopy(xyz.get());
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
  grpc_core::Map<UniquePtr<char>, RefCountedPtr<ReffedPayload>, StringLess>
      test_map;
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  UniquePtr<char> klm_copy = getStringCopy(klm.get());
  UniquePtr<char> xyz_copy = getStringCopy(xyz.get());
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
  grpc_core::Map<UniquePtr<char>, OrphanablePtr<OrphanablePayload>, StringLess>
      test_map;
  OrphanablePtr<OrphanablePayload> p[3];
  for (int i = 0; i < 3; i++) {
    p[i] = MakeOrphanable<OrphanablePayload>(i);
  }
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  test_map.emplace(std::move(abc_copy), std::move(p[0]));
  test_map.emplace(std::move(efg_copy), std::move(p[1]));
  test_map.emplace(std::move(hij_copy), std::move(p[2]));
  EXPECT_EQ(0, test_map.find(abc)->second->data());
  EXPECT_EQ(1, test_map.find(efg)->second->data());
  EXPECT_EQ(2, test_map.find(hij)->second->data());
}

// Test insertion of unique pointer values
TEST_F(MapTest, MapAddTestUniquePtr) {
  grpc_core::Map<UniquePtr<char>, UniquePtr<Payload>, StringLess> test_map;
  UniquePtr<Payload> p[3];
  for (int i = 0; i < 3; i++) {
    p[i] = MakeUnique<Payload>(i);
  }
  UniquePtr<char> abc_copy = getStringCopy(abc.get());
  UniquePtr<char> efg_copy = getStringCopy(efg.get());
  UniquePtr<char> hij_copy = getStringCopy(hij.get());
  test_map.emplace(std::move(abc_copy), std::move(p[0]));
  test_map.emplace(std::move(efg_copy), std::move(p[1]));
  test_map.emplace(std::move(hij_copy), std::move(p[2]));
  EXPECT_EQ(0, test_map.find(abc)->second->data());
  EXPECT_EQ(1, test_map.find(efg)->second->data());
  EXPECT_EQ(2, test_map.find(hij)->second->data());
}
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
