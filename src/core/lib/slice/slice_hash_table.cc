//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "src/core/lib/slice/slice_hash_table.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/metadata.h"

struct grpc_slice_hash_table {
  gpr_refcount refs;
  void (*destroy_value)(void* value);
  int (*value_cmp)(void* a, void* b);
  size_t size;
  size_t max_num_probes;
  grpc_slice_hash_table_entry* entries;
};

static bool is_empty(grpc_slice_hash_table_entry* entry) {
  return entry->value == nullptr;
}

static void grpc_slice_hash_table_add(grpc_slice_hash_table* table,
                                      grpc_slice key, void* value) {
  GPR_ASSERT(value != nullptr);
  const size_t hash = grpc_slice_hash(key);
  for (size_t offset = 0; offset < table->size; ++offset) {
    const size_t idx = (hash + offset) % table->size;
    if (is_empty(&table->entries[idx])) {
      table->entries[idx].key = key;
      table->entries[idx].value = value;
      // Keep track of the maximum number of probes needed, since this
      // provides an upper bound for lookups.
      if (offset > table->max_num_probes) table->max_num_probes = offset;
      return;
    }
  }
  GPR_ASSERT(false);  // Table should never be full.
}

grpc_slice_hash_table* grpc_slice_hash_table_create(
    size_t num_entries, grpc_slice_hash_table_entry* entries,
    void (*destroy_value)(void* value), int (*value_cmp)(void* a, void* b)) {
  grpc_slice_hash_table* table =
      static_cast<grpc_slice_hash_table*>(gpr_zalloc(sizeof(*table)));
  gpr_ref_init(&table->refs, 1);
  table->destroy_value = destroy_value;
  table->value_cmp = value_cmp;
  // Keep load factor low to improve performance of lookups.
  table->size = num_entries * 2;
  const size_t entry_size = sizeof(grpc_slice_hash_table_entry) * table->size;
  table->entries =
      static_cast<grpc_slice_hash_table_entry*>(gpr_zalloc(entry_size));
  for (size_t i = 0; i < num_entries; ++i) {
    grpc_slice_hash_table_entry* entry = &entries[i];
    grpc_slice_hash_table_add(table, entry->key, entry->value);
  }
  return table;
}

grpc_slice_hash_table* grpc_slice_hash_table_ref(grpc_slice_hash_table* table) {
  if (table != nullptr) gpr_ref(&table->refs);
  return table;
}

void grpc_slice_hash_table_unref(grpc_slice_hash_table* table) {
  if (table != nullptr && gpr_unref(&table->refs)) {
    for (size_t i = 0; i < table->size; ++i) {
      grpc_slice_hash_table_entry* entry = &table->entries[i];
      if (!is_empty(entry)) {
        grpc_slice_unref_internal(entry->key);
        table->destroy_value(entry->value);
      }
    }
    gpr_free(table->entries);
    gpr_free(table);
  }
}

void* grpc_slice_hash_table_get(const grpc_slice_hash_table* table,
                                const grpc_slice key) {
  const size_t hash = grpc_slice_hash(key);
  // We cap the number of probes at the max number recorded when
  // populating the table.
  for (size_t offset = 0; offset <= table->max_num_probes; ++offset) {
    const size_t idx = (hash + offset) % table->size;
    if (is_empty(&table->entries[idx])) break;
    if (grpc_slice_eq(table->entries[idx].key, key)) {
      return table->entries[idx].value;
    }
  }
  return nullptr;  // Not found.
}

static int pointer_cmp(void* a, void* b) { return GPR_ICMP(a, b); }
int grpc_slice_hash_table_cmp(const grpc_slice_hash_table* a,
                              const grpc_slice_hash_table* b) {
  int (*const value_cmp_fn_a)(void* a, void* b) =
      a->value_cmp != nullptr ? a->value_cmp : pointer_cmp;
  int (*const value_cmp_fn_b)(void* a, void* b) =
      b->value_cmp != nullptr ? b->value_cmp : pointer_cmp;
  // Compare value_fns
  const int value_fns_cmp =
      GPR_ICMP((void*)value_cmp_fn_a, (void*)value_cmp_fn_b);
  if (value_fns_cmp != 0) return value_fns_cmp;
  // Compare sizes
  if (a->size < b->size) return -1;
  if (a->size > b->size) return 1;
  // Compare rows.
  for (size_t i = 0; i < a->size; ++i) {
    if (is_empty(&a->entries[i])) {
      if (!is_empty(&b->entries[i])) {
        return -1;  // a empty but b non-empty
      }
      continue;  // both empty, no need to check key or value
    } else if (is_empty(&b->entries[i])) {
      return 1;  // a non-empty but b empty
    }
    // neither entry is empty
    const int key_cmp = grpc_slice_cmp(a->entries[i].key, b->entries[i].key);
    if (key_cmp != 0) return key_cmp;
    const int value_cmp =
        value_cmp_fn_a(a->entries[i].value, b->entries[i].value);
    if (value_cmp != 0) return value_cmp;
  }
  return 0;
}
