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
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

grpc_slice BuildRefCountedKey(const char* key_str) {
  const size_t key_length = strlen(key_str);
  grpc_slice key = grpc_slice_malloc_large(key_length);
  memcpy(GRPC_SLICE_START_PTR(key), key_str, key_length);
  return key;
}

TEST(SliceWeakHashTable, Basic) {
  auto table = SliceWeakHashTable<UniquePtr<char>, 10>::Create();
  // Single key-value insertion.
  grpc_slice key = BuildRefCountedKey("key");
  grpc_slice_ref(key);  // Get doesn't own.
  table->Add(key, UniquePtr<char>(gpr_strdup("value")));
  ASSERT_NE(table->Get(key), nullptr);
  ASSERT_STREQ(table->Get(key)->get(), "value");
  grpc_slice_unref(key);
  // Unknown key.
  ASSERT_EQ(table->Get(grpc_slice_from_static_string("unknown_key")), nullptr);
}

TEST(SliceWeakHashTable, ValueTypeConstructor) {
  struct Value {
    Value() : a(123) {}
    int a;
  };
  auto table = SliceWeakHashTable<Value, 1>::Create();
  grpc_slice key = BuildRefCountedKey("key");
  grpc_slice_ref(key);  // Get doesn't own.
  table->Add(key, Value());
  ASSERT_EQ(table->Get(key)->a, 123);
  grpc_slice_unref(key);
}

TEST(SliceWeakHashTable, ForceOverload) {
  constexpr int kTableSize = 10;
  auto table = SliceWeakHashTable<UniquePtr<char>, kTableSize>::Create();
  // Insert a multiple of the maximum size table.
  for (int i = 0; i < kTableSize * 2; ++i) {
    std::ostringstream oss;
    oss << "key-" << i;
    grpc_slice key = BuildRefCountedKey(oss.str().c_str());
    oss.clear();
    oss << "value-" << i;
    table->Add(key, UniquePtr<char>(gpr_strdup(oss.str().c_str())));
  }
  // Verify that some will have been replaced.
  int num_missing = 0;
  for (int i = 0; i < kTableSize * 2; ++i) {
    std::ostringstream oss;
    oss << "key-" << i;
    grpc_slice key = BuildRefCountedKey(oss.str().c_str());
    if (table->Get(key) == nullptr) num_missing++;
    grpc_slice_unref(key);
  }
  // At least kTableSize elements will be missing.
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
