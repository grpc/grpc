//
// Copyright 2016, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
  void (*destroy_value)(grpc_exec_ctx* exec_ctx, void* value);
  int (*value_cmp)(void* a, void* b);
  size_t size;
  size_t max_num_probes;
  grpc_slice_hash_table_entry* entries;
};

static bool is_empty(grpc_slice_hash_table_entry* entry) {
  return entry->value == NULL;
}

static void grpc_slice_hash_table_add(grpc_slice_hash_table* table,
                                      grpc_slice key, void* value) {
  GPR_ASSERT(value != NULL);
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
    void (*destroy_value)(grpc_exec_ctx* exec_ctx, void* value),
    int (*value_cmp)(void* a, void* b)) {
  grpc_slice_hash_table* table = gpr_zalloc(sizeof(*table));
  gpr_ref_init(&table->refs, 1);
  table->destroy_value = destroy_value;
  table->value_cmp = value_cmp;
  // Keep load factor low to improve performance of lookups.
  table->size = num_entries * 2;
  const size_t entry_size = sizeof(grpc_slice_hash_table_entry) * table->size;
  table->entries = gpr_zalloc(entry_size);
  for (size_t i = 0; i < num_entries; ++i) {
    grpc_slice_hash_table_entry* entry = &entries[i];
    grpc_slice_hash_table_add(table, entry->key, entry->value);
  }
  return table;
}

grpc_slice_hash_table* grpc_slice_hash_table_ref(grpc_slice_hash_table* table) {
  if (table != NULL) gpr_ref(&table->refs);
  return table;
}

void grpc_slice_hash_table_unref(grpc_exec_ctx* exec_ctx,
                                 grpc_slice_hash_table* table) {
  if (table != NULL && gpr_unref(&table->refs)) {
    for (size_t i = 0; i < table->size; ++i) {
      grpc_slice_hash_table_entry* entry = &table->entries[i];
      if (!is_empty(entry)) {
        grpc_slice_unref_internal(exec_ctx, entry->key);
        table->destroy_value(exec_ctx, entry->value);
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
  return NULL;  // Not found.
}

static int pointer_cmp(void* a, void* b) { return GPR_ICMP(a, b); }
int grpc_slice_hash_table_cmp(const grpc_slice_hash_table* a,
                              const grpc_slice_hash_table* b) {
  int (*const value_cmp_fn_a)(void* a, void* b) =
      a->value_cmp != NULL ? a->value_cmp : pointer_cmp;
  int (*const value_cmp_fn_b)(void* a, void* b) =
      b->value_cmp != NULL ? b->value_cmp : pointer_cmp;
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
