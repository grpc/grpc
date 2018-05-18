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

#include "src/core/lib/slice/slice_hash_table.h"

#include <string.h>

#include <vector>

#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

typedef SliceHashTable<UniquePtr<char>> TestHashTable;

struct TestEntry {
  const char* key;
  const char* value;
};

void CheckValues(const std::vector<TestEntry>& input,
                 const TestHashTable& table) {
  for (const TestEntry& expected : input) {
    grpc_slice key = grpc_slice_from_static_string(expected.key);
    const UniquePtr<char>* actual = table.Get(key);
    ASSERT_NE(actual, nullptr);
    EXPECT_STREQ(expected.value, actual->get());
    grpc_slice_unref(key);
  }
}

void CheckNonExistentValue(const char* key_string, const TestHashTable& table) {
  grpc_slice key = grpc_slice_from_static_string(key_string);
  ASSERT_EQ(nullptr, table.Get(key));
  grpc_slice_unref(key);
}

void PopulateEntries(const std::vector<TestEntry>& input,
                     TestHashTable::Entry* output) {
  for (size_t i = 0; i < input.size(); ++i) {
    output[i].key = grpc_slice_from_copied_string(input[i].key);
    output[i].value = UniquePtr<char>(gpr_strdup(input[i].value));
  }
}

RefCountedPtr<TestHashTable> CreateTableFromEntries(
    const std::vector<TestEntry>& test_entries,
    TestHashTable::ValueCmp value_cmp) {
  TestHashTable::Entry* entries = static_cast<TestHashTable::Entry*>(
      gpr_zalloc(sizeof(*entries) * test_entries.size()));
  PopulateEntries(test_entries, entries);
  RefCountedPtr<TestHashTable> table =
      TestHashTable::Create(test_entries.size(), entries, value_cmp);
  gpr_free(entries);
  return table;
}

TEST(SliceHashTable, Basic) {
  const std::vector<TestEntry> test_entries = {
      {"key_0", "value_0"},   {"key_1", "value_1"},   {"key_2", "value_2"},
      {"key_3", "value_3"},   {"key_4", "value_4"},   {"key_5", "value_5"},
      {"key_6", "value_6"},   {"key_7", "value_7"},   {"key_8", "value_8"},
      {"key_9", "value_9"},   {"key_10", "value_10"}, {"key_11", "value_11"},
      {"key_12", "value_12"}, {"key_13", "value_13"}, {"key_14", "value_14"},
      {"key_15", "value_15"}, {"key_16", "value_16"}, {"key_17", "value_17"},
      {"key_18", "value_18"}, {"key_19", "value_19"}, {"key_20", "value_20"},
      {"key_21", "value_21"}, {"key_22", "value_22"}, {"key_23", "value_23"},
      {"key_24", "value_24"}, {"key_25", "value_25"}, {"key_26", "value_26"},
      {"key_27", "value_27"}, {"key_28", "value_28"}, {"key_29", "value_29"},
      {"key_30", "value_30"}, {"key_31", "value_31"}, {"key_32", "value_32"},
      {"key_33", "value_33"}, {"key_34", "value_34"}, {"key_35", "value_35"},
      {"key_36", "value_36"}, {"key_37", "value_37"}, {"key_38", "value_38"},
      {"key_39", "value_39"}, {"key_40", "value_40"}, {"key_41", "value_41"},
      {"key_42", "value_42"}, {"key_43", "value_43"}, {"key_44", "value_44"},
      {"key_45", "value_45"}, {"key_46", "value_46"}, {"key_47", "value_47"},
      {"key_48", "value_48"}, {"key_49", "value_49"}, {"key_50", "value_50"},
      {"key_51", "value_51"}, {"key_52", "value_52"}, {"key_53", "value_53"},
      {"key_54", "value_54"}, {"key_55", "value_55"}, {"key_56", "value_56"},
      {"key_57", "value_57"}, {"key_58", "value_58"}, {"key_59", "value_59"},
      {"key_60", "value_60"}, {"key_61", "value_61"}, {"key_62", "value_62"},
      {"key_63", "value_63"}, {"key_64", "value_64"}, {"key_65", "value_65"},
      {"key_66", "value_66"}, {"key_67", "value_67"}, {"key_68", "value_68"},
      {"key_69", "value_69"}, {"key_70", "value_70"}, {"key_71", "value_71"},
      {"key_72", "value_72"}, {"key_73", "value_73"}, {"key_74", "value_74"},
      {"key_75", "value_75"}, {"key_76", "value_76"}, {"key_77", "value_77"},
      {"key_78", "value_78"}, {"key_79", "value_79"}, {"key_80", "value_80"},
      {"key_81", "value_81"}, {"key_82", "value_82"}, {"key_83", "value_83"},
      {"key_84", "value_84"}, {"key_85", "value_85"}, {"key_86", "value_86"},
      {"key_87", "value_87"}, {"key_88", "value_88"}, {"key_89", "value_89"},
      {"key_90", "value_90"}, {"key_91", "value_91"}, {"key_92", "value_92"},
      {"key_93", "value_93"}, {"key_94", "value_94"}, {"key_95", "value_95"},
      {"key_96", "value_96"}, {"key_97", "value_97"}, {"key_98", "value_98"},
      {"key_99", "value_99"},
  };
  RefCountedPtr<TestHashTable> table =
      CreateTableFromEntries(test_entries, nullptr);
  // Check contents of table.
  CheckValues(test_entries, *table);
  CheckNonExistentValue("XX", *table);
}

int StringCmp(const UniquePtr<char>& a, const UniquePtr<char>& b) {
  return strcmp(a.get(), b.get());
}

int PointerCmp(const UniquePtr<char>& a, const UniquePtr<char>& b) {
  return GPR_ICMP(a.get(), b.get());
}

TEST(SliceHashTable, CmpEqual) {
  const std::vector<TestEntry> test_entries_a = {
      {"key_0", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  RefCountedPtr<TestHashTable> table_a =
      CreateTableFromEntries(test_entries_a, StringCmp);
  const std::vector<TestEntry> test_entries_b = {
      {"key_0", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  RefCountedPtr<TestHashTable> table_b =
      CreateTableFromEntries(test_entries_b, StringCmp);
  // table_a equals itself.
  EXPECT_EQ(0, TestHashTable::Cmp(*table_a, *table_a));
  // table_a equals table_b.
  EXPECT_EQ(0, TestHashTable::Cmp(*table_a, *table_b));
}

TEST(SliceHashTable, CmpDifferentSizes) {
  // table_a has 3 entries, table_b has only 2.
  const std::vector<TestEntry> test_entries_a = {
      {"key_0", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  RefCountedPtr<TestHashTable> table_a =
      CreateTableFromEntries(test_entries_a, StringCmp);
  const std::vector<TestEntry> test_entries_b = {{"key_0", "value_0"},
                                                 {"key_1", "value_1"}};
  RefCountedPtr<TestHashTable> table_b =
      CreateTableFromEntries(test_entries_b, StringCmp);
  EXPECT_GT(TestHashTable::Cmp(*table_a, *table_b), 0);
  EXPECT_LT(TestHashTable::Cmp(*table_b, *table_a), 0);
}

TEST(SliceHashTable, CmpDifferentKey) {
  // One key doesn't match and is lexicographically "smaller".
  const std::vector<TestEntry> test_entries_a = {
      {"key_0", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  RefCountedPtr<TestHashTable> table_a =
      CreateTableFromEntries(test_entries_a, StringCmp);
  const std::vector<TestEntry> test_entries_b = {
      {"key_zz", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  RefCountedPtr<TestHashTable> table_b =
      CreateTableFromEntries(test_entries_b, StringCmp);
  EXPECT_GT(TestHashTable::Cmp(*table_a, *table_b), 0);
  EXPECT_LT(TestHashTable::Cmp(*table_b, *table_a), 0);
}

TEST(SliceHashTable, CmpDifferentValue) {
  // One value doesn't match.
  const std::vector<TestEntry> test_entries_a = {
      {"key_0", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  RefCountedPtr<TestHashTable> table_a =
      CreateTableFromEntries(test_entries_a, StringCmp);
  const std::vector<TestEntry> test_entries_b = {
      {"key_0", "value_z"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  RefCountedPtr<TestHashTable> table_b =
      CreateTableFromEntries(test_entries_b, StringCmp);
  EXPECT_LT(TestHashTable::Cmp(*table_a, *table_b), 0);
  EXPECT_GT(TestHashTable::Cmp(*table_b, *table_a), 0);
}

TEST(SliceHashTable, CmpDifferentCmpFunctions) {
  // Same values but different "equals" functions.
  const std::vector<TestEntry> test_entries_a = {
      {"key_0", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  RefCountedPtr<TestHashTable> table_a =
      CreateTableFromEntries(test_entries_a, StringCmp);
  const std::vector<TestEntry> test_entries_b = {
      {"key_0", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  RefCountedPtr<TestHashTable> table_b =
      CreateTableFromEntries(test_entries_b, PointerCmp);
  EXPECT_NE(TestHashTable::Cmp(*table_a, *table_b), 0);
}

TEST(SliceHashTable, CmpEmptyKeysDifferentValue) {
  // Same (empty) key, different values.
  const std::vector<TestEntry> test_entries_a = {{"", "value_0"}};
  RefCountedPtr<TestHashTable> table_a =
      CreateTableFromEntries(test_entries_a, StringCmp);
  const std::vector<TestEntry> test_entries_b = {{"", "value_1"}};
  RefCountedPtr<TestHashTable> table_b =
      CreateTableFromEntries(test_entries_b, PointerCmp);
  EXPECT_NE(TestHashTable::Cmp(*table_a, *table_b), 0);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_test_init(argc, argv);
  grpc_core::ExecCtx::GlobalInit();
  int result = RUN_ALL_TESTS();
  grpc_core::ExecCtx::GlobalShutdown();
  return result;
}
