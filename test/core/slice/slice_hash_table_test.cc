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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

typedef struct {
  const char* key;
  const char* value;
} test_entry;

static void populate_entries(const test_entry* input, size_t num_entries,
                             grpc_slice_hash_table_entry* output) {
  for (size_t i = 0; i < num_entries; ++i) {
    output[i].key = grpc_slice_from_copied_string(input[i].key);
    output[i].value = gpr_strdup(input[i].value);
  }
}

static void check_values(const test_entry* input, size_t num_entries,
                         grpc_slice_hash_table* table) {
  for (size_t i = 0; i < num_entries; ++i) {
    grpc_slice key = grpc_slice_from_static_string(input[i].key);
    const char* actual =
        static_cast<const char*>(grpc_slice_hash_table_get(table, key));
    GPR_ASSERT(actual != nullptr);
    GPR_ASSERT(strcmp(actual, input[i].value) == 0);
    grpc_slice_unref(key);
  }
}

static void check_non_existent_value(const char* key_string,
                                     grpc_slice_hash_table* table) {
  grpc_slice key = grpc_slice_from_static_string(key_string);
  GPR_ASSERT(grpc_slice_hash_table_get(table, key) == nullptr);
  grpc_slice_unref(key);
}

static void destroy_string(grpc_exec_ctx* exec_ctx, void* value) {
  gpr_free(value);
}

static grpc_slice_hash_table* create_table_from_entries(
    const test_entry* test_entries, size_t num_test_entries,
    int (*value_cmp_fn)(void*, void*)) {
  // Construct table.
  grpc_slice_hash_table_entry* entries =
      static_cast<grpc_slice_hash_table_entry*>(
          gpr_zalloc(sizeof(*entries) * num_test_entries));
  populate_entries(test_entries, num_test_entries, entries);
  grpc_slice_hash_table* table = grpc_slice_hash_table_create(
      num_test_entries, entries, destroy_string, value_cmp_fn);
  gpr_free(entries);
  return table;
}

static void test_slice_hash_table() {
  const test_entry test_entries[] = {
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
  const size_t num_entries = GPR_ARRAY_SIZE(test_entries);
  grpc_slice_hash_table* table =
      create_table_from_entries(test_entries, num_entries, nullptr);
  // Check contents of table.
  check_values(test_entries, num_entries, table);
  check_non_existent_value("XX", table);
  // Clean up.
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_slice_hash_table_unref(&exec_ctx, table);
  grpc_exec_ctx_finish(&exec_ctx);
}

static int value_cmp_fn(void* a, void* b) {
  const char* a_str = static_cast<const char*>(a);
  const char* b_str = static_cast<const char*>(b);
  return strcmp(a_str, b_str);
}

static int pointer_cmp_fn(void* a, void* b) { return GPR_ICMP(a, b); }

static void test_slice_hash_table_eq() {
  const test_entry test_entries_a[] = {
      {"key_0", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  const size_t num_entries_a = GPR_ARRAY_SIZE(test_entries_a);
  grpc_slice_hash_table* table_a =
      create_table_from_entries(test_entries_a, num_entries_a, value_cmp_fn);
  GPR_ASSERT(grpc_slice_hash_table_cmp(table_a, table_a) == 0);

  const test_entry test_entries_b[] = {
      {"key_0", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  const size_t num_entries_b = GPR_ARRAY_SIZE(test_entries_b);
  grpc_slice_hash_table* table_b =
      create_table_from_entries(test_entries_b, num_entries_b, value_cmp_fn);

  GPR_ASSERT(grpc_slice_hash_table_cmp(table_a, table_b) == 0);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_slice_hash_table_unref(&exec_ctx, table_a);
  grpc_slice_hash_table_unref(&exec_ctx, table_b);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_slice_hash_table_not_eq() {
  const test_entry test_entries_a[] = {
      {"key_0", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  const size_t num_entries_a = GPR_ARRAY_SIZE(test_entries_a);
  grpc_slice_hash_table* table_a =
      create_table_from_entries(test_entries_a, num_entries_a, value_cmp_fn);

  // Different sizes.
  const test_entry test_entries_b_smaller[] = {{"key_0", "value_0"},
                                               {"key_1", "value_1"}};
  const size_t num_entries_b_smaller = GPR_ARRAY_SIZE(test_entries_b_smaller);
  grpc_slice_hash_table* table_b_smaller = create_table_from_entries(
      test_entries_b_smaller, num_entries_b_smaller, value_cmp_fn);
  GPR_ASSERT(grpc_slice_hash_table_cmp(table_a, table_b_smaller) > 0);

  const test_entry test_entries_b_larger[] = {{"key_0", "value_0"},
                                              {"key_1", "value_1"},
                                              {"key_2", "value_2"},
                                              {"key_3", "value_3"}};
  const size_t num_entries_b_larger = GPR_ARRAY_SIZE(test_entries_b_larger);
  grpc_slice_hash_table* table_b_larger = create_table_from_entries(
      test_entries_b_larger, num_entries_b_larger, value_cmp_fn);
  GPR_ASSERT(grpc_slice_hash_table_cmp(table_a, table_b_larger) < 0);

  // One key doesn't match and is lexicographically "smaller".
  const test_entry test_entries_c[] = {
      {"key_zz", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  const size_t num_entries_c = GPR_ARRAY_SIZE(test_entries_c);
  grpc_slice_hash_table* table_c =
      create_table_from_entries(test_entries_c, num_entries_c, value_cmp_fn);
  GPR_ASSERT(grpc_slice_hash_table_cmp(table_a, table_c) > 0);
  GPR_ASSERT(grpc_slice_hash_table_cmp(table_c, table_a) < 0);

  // One value doesn't match.
  const test_entry test_entries_d[] = {
      {"key_0", "value_z"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  const size_t num_entries_d = GPR_ARRAY_SIZE(test_entries_d);
  grpc_slice_hash_table* table_d =
      create_table_from_entries(test_entries_d, num_entries_d, value_cmp_fn);
  GPR_ASSERT(grpc_slice_hash_table_cmp(table_a, table_d) < 0);
  GPR_ASSERT(grpc_slice_hash_table_cmp(table_d, table_a) > 0);

  // Same values but different "equals" functions.
  const test_entry test_entries_e[] = {
      {"key_0", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  const size_t num_entries_e = GPR_ARRAY_SIZE(test_entries_e);
  grpc_slice_hash_table* table_e =
      create_table_from_entries(test_entries_e, num_entries_e, value_cmp_fn);
  const test_entry test_entries_f[] = {
      {"key_0", "value_0"}, {"key_1", "value_1"}, {"key_2", "value_2"}};
  const size_t num_entries_f = GPR_ARRAY_SIZE(test_entries_f);
  grpc_slice_hash_table* table_f =
      create_table_from_entries(test_entries_f, num_entries_f, pointer_cmp_fn);
  GPR_ASSERT(grpc_slice_hash_table_cmp(table_e, table_f) != 0);

  // Same (empty) key, different values.
  const test_entry test_entries_g[] = {{"", "value_0"}};
  const size_t num_entries_g = GPR_ARRAY_SIZE(test_entries_g);
  grpc_slice_hash_table* table_g =
      create_table_from_entries(test_entries_g, num_entries_g, value_cmp_fn);
  const test_entry test_entries_h[] = {{"", "value_1"}};
  const size_t num_entries_h = GPR_ARRAY_SIZE(test_entries_h);
  grpc_slice_hash_table* table_h =
      create_table_from_entries(test_entries_h, num_entries_h, pointer_cmp_fn);
  GPR_ASSERT(grpc_slice_hash_table_cmp(table_g, table_h) != 0);

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_slice_hash_table_unref(&exec_ctx, table_a);
  grpc_slice_hash_table_unref(&exec_ctx, table_b_larger);
  grpc_slice_hash_table_unref(&exec_ctx, table_b_smaller);
  grpc_slice_hash_table_unref(&exec_ctx, table_c);
  grpc_slice_hash_table_unref(&exec_ctx, table_d);
  grpc_slice_hash_table_unref(&exec_ctx, table_e);
  grpc_slice_hash_table_unref(&exec_ctx, table_f);
  grpc_slice_hash_table_unref(&exec_ctx, table_g);
  grpc_slice_hash_table_unref(&exec_ctx, table_h);
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  test_slice_hash_table();
  test_slice_hash_table_eq();
  test_slice_hash_table_not_eq();
  return 0;
}
