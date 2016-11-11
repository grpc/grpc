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

static grpc_slice_refcount terminal_slice_refcount = {0};
static const grpc_slice terminal_slice = {&terminal_slice_refcount,
                                          .data.refcounted = {0, 0}};

struct grpc_slice_hash_table {
  gpr_refcount refs;
  size_t num_entries;
  size_t size;
  grpc_slice_hash_table_entry* entries;
};

static bool is_terminal(grpc_slice slice) {
  return slice.refcount == &terminal_slice_refcount;
}

// Helper function for insert and get operations that performs quadratic
// probing (https://en.wikipedia.org/wiki/Quadratic_probing).
static size_t grpc_slice_hash_table_find_index(
    const grpc_slice_hash_table* table, const grpc_slice key, bool find_empty) {
  size_t hash = grpc_slice_hash(key);
  for (size_t i = 0; i < table->size; ++i) {
    const size_t idx = (hash + i * i) % table->size;
    if (is_terminal(table->entries[idx].key)) {
      return find_empty ? idx : table->size;
    }
    if (grpc_slice_cmp(table->entries[idx].key, key) == 0) {
      return idx;
    }
  }
  return table->size;  // Not found.
}

static void grpc_slice_hash_table_add(
    grpc_slice_hash_table* table, grpc_slice key, void* value,
    const grpc_slice_hash_table_vtable* vtable) {
  GPR_ASSERT(value != NULL);
  const size_t idx =
      grpc_slice_hash_table_find_index(table, key, true /* find_empty */);
  GPR_ASSERT(idx != table->size);  // Table should never be full.
  grpc_slice_hash_table_entry* entry = &table->entries[idx];
  entry->key = grpc_slice_ref(key);
  entry->value = vtable->copy_value(value);
  entry->vtable = vtable;
}

grpc_slice_hash_table* grpc_slice_hash_table_create(
    size_t num_entries, grpc_slice_hash_table_entry* entries) {
  grpc_slice_hash_table* table = gpr_malloc(sizeof(*table));
  memset(table, 0, sizeof(*table));
  gpr_ref_init(&table->refs, 1);
  table->num_entries = num_entries;
  // Quadratic probing gets best performance when the table is no more
  // than half full.
  table->size = num_entries * 2;
  const size_t entry_size = sizeof(grpc_slice_hash_table_entry) * table->size;
  table->entries = gpr_malloc(entry_size);
  memset(table->entries, 0, entry_size);
  for (size_t i = 0; i < num_entries; ++i) {
    table->entries[i].key = terminal_slice;
  }
  for (size_t i = 0; i < num_entries; ++i) {
    grpc_slice_hash_table_entry* entry = &entries[i];
    grpc_slice_hash_table_add(table, entry->key, entry->value, entry->vtable);
  }
  return table;
}

grpc_slice_hash_table* grpc_slice_hash_table_ref(grpc_slice_hash_table* table) {
  if (table != NULL) gpr_ref(&table->refs);
  return table;
}

int grpc_slice_hash_table_unref(grpc_exec_ctx* exec_ctx,
                                grpc_slice_hash_table* table) {
  if (table != NULL && gpr_unref(&table->refs)) {
    for (size_t i = 0; i < table->size; ++i) {
      grpc_slice_hash_table_entry* entry = &table->entries[i];
      if (!is_terminal(entry->key)) {
        grpc_slice_unref_internal(exec_ctx, entry->key);
        entry->vtable->destroy_value(exec_ctx, entry->value);
      }
    }
    gpr_free(table->entries);
    gpr_free(table);
    return 1;
  }
  return 0;
}

size_t grpc_slice_hash_table_num_entries(const grpc_slice_hash_table* table) {
  return table->num_entries;
}

void* grpc_slice_hash_table_get(const grpc_slice_hash_table* table,
                                const grpc_slice key) {
  const size_t idx =
      grpc_slice_hash_table_find_index(table, key, false /* find_empty */);
  if (idx == table->size) return NULL;  // Not found.
  return table->entries[idx].value;
}

int grpc_slice_hash_table_cmp(const grpc_slice_hash_table* table1,
                              const grpc_slice_hash_table* table2) {
  // Compare by num_entries.
  if (table1->num_entries < table2->num_entries) return -1;
  if (table1->num_entries > table2->num_entries) return 1;
  for (size_t i = 0; i < table1->num_entries; ++i) {
    grpc_slice_hash_table_entry* e1 = &table1->entries[i];
    grpc_slice_hash_table_entry* e2 = &table2->entries[i];
    // Compare keys by hash value.
    int cmp = grpc_slice_cmp(e1->key, e2->key);
    if (cmp != 0) return cmp;
    // Compare by vtable (pointer equality).
    if (e1->vtable < e2->vtable) return -1;
    if (e1->vtable > e2->vtable) return 1;
    // Compare values via vtable.
    const int value_result = e1->vtable->compare_value(e1->value, e2->value);
    if (value_result != 0) return value_result;
  }
  return 0;
}

void grpc_slice_hash_table_iterate(
    const grpc_slice_hash_table* table,
    void (*func)(const grpc_slice_hash_table_entry* entry, void* user_data),
    void* user_data) {
  for (size_t i = 0; i < table->size; ++i) {
    if (!is_terminal(table->entries[i].key)) {
      func(&table->entries[i], user_data);
    }
  }
}
