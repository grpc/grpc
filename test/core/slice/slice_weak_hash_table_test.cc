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

#include "src/core/lib/slice/slice_weak_hash_table.h"

#include <cstring>
#include <sstream>

#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

TEST(SliceWeakHashTable, Basic) {
  auto table = SliceWeakHashTable<UniquePtr<char>>::Create(10);
  // Single key-value insertion.
  grpc_slice key = grpc_slice_from_copied_string("key");
  table->Add(key, UniquePtr<char>(gpr_strdup("value")));
  ASSERT_NE(table->Get(key), nullptr);
  ASSERT_STREQ(table->Get(key)->get(), "value");
  grpc_slice_unref(key);
  // Unknown key.
  ASSERT_EQ(table->Get(grpc_slice_from_static_string("unknown_key")), nullptr);
}

TEST(SliceWeakHashTable, LongKeys) {
  auto table = SliceWeakHashTable<UniquePtr<char>>::Create(10);
  // Single key-value insertion.
  grpc_slice long_key = grpc_slice_malloc_large(64);
  const char key_str[] = "this key is long and won't be inlined";
  memcpy(GRPC_SLICE_START_PTR(long_key), key_str, GPR_ARRAY_SIZE(key_str));
  table->Add(long_key, UniquePtr<char>(gpr_strdup("value")));
  ASSERT_NE(table->Get(long_key), nullptr);
  ASSERT_STREQ(table->Get(long_key)->get(), "value");
  grpc_slice_unref(long_key);
  // Unknown key.
  ASSERT_EQ(table->Get(grpc_slice_from_static_string("unknown_key")), nullptr);
}

TEST(SliceWeakHashTable, Update) {
  auto table = SliceWeakHashTable<int>::Create(10);
  grpc_slice key = grpc_slice_from_copied_string("key");
  // Updates for non-existing keys are no-ops.
  table->Update(key, 12345);
  ASSERT_EQ(table->Get(key), nullptr);
  // But updates over existing ones work.
  table->Add(key, 31416);
  ASSERT_NE(table->Get(key), nullptr);
  table->Update(key, 27182);
  ASSERT_EQ(*table->Get(key), 27182);
  grpc_slice_unref(key);
}

TEST(SliceWeakHashTable, Get) {
  const auto& table = SliceWeakHashTable<int>::Create(10);
  grpc_slice key = grpc_slice_from_copied_string("key");
  table->Add(key, 31416);
  ASSERT_NE(table->Get(key), nullptr);
  const auto* value = table->Get(key);
  ASSERT_EQ(*value, 31416);
  grpc_slice_unref(key);
}

TEST(SliceWeakHashTable, ForceOverload) {
  constexpr int kTableSize = 10;
  auto table = SliceWeakHashTable<UniquePtr<char>>::Create(kTableSize);
  // Insert a multiple of the maximum size table.
  for (int i = 0; i < kTableSize * 2; ++i) {
    std::ostringstream oss;
    oss << "key-" << i;
    grpc_slice key = grpc_slice_from_copied_string(oss.str().c_str());
    oss.clear();
    oss << "value-" << i;
    table->Add(key, UniquePtr<char>(gpr_strdup(oss.str().c_str())));
    grpc_slice_unref(key);
  }
  // Verify that some will have been replaced.
  int num_missing = 0;
  for (int i = 0; i < kTableSize * 2; ++i) {
    std::ostringstream oss;
    oss << "key-" << i;
    grpc_slice key = grpc_slice_from_copied_string(oss.str().c_str());
    if (table->Get(key) == nullptr) num_missing++;
    grpc_slice_unref(key);
  }
  // At least kTableSize elemens will be missing.
  ASSERT_GE(num_missing, kTableSize);
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
