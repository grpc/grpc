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

#include "src/core/lib/transport/mdstr_hash_table.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/transport/metadata.h"

struct grpc_mdstr_hash_table {
  gpr_refcount refs;
  size_t num_entries;
  size_t size;
  grpc_mdstr_hash_table_entry* entries;
};

// Helper function for insert and get operations that performs quadratic
// probing (https://en.wikipedia.org/wiki/Quadratic_probing).
static size_t grpc_mdstr_hash_table_find_index(
    const grpc_mdstr_hash_table* table, const grpc_mdstr* key,
    bool find_empty) {
  for (size_t i = 0; i < table->size; ++i) {
    const size_t idx = (key->hash + i * i) % table->size;
    if (table->entries[idx].key == NULL) return find_empty ? idx : table->size;
    if (table->entries[idx].key == key) return idx;
  }
  return table->size;  // Not found.
}

static void grpc_mdstr_hash_table_add(
    grpc_mdstr_hash_table* table, grpc_mdstr* key, void* value,
    const grpc_mdstr_hash_table_vtable* vtable) {
  GPR_ASSERT(value != NULL);
  const size_t idx =
      grpc_mdstr_hash_table_find_index(table, key, true /* find_empty */);
  GPR_ASSERT(idx != table->size);  // Table should never be full.
  grpc_mdstr_hash_table_entry* entry = &table->entries[idx];
  entry->key = GRPC_MDSTR_REF(key);
  entry->value = vtable->copy_value(value);
  entry->vtable = vtable;
}

grpc_mdstr_hash_table* grpc_mdstr_hash_table_create(
    size_t num_entries, grpc_mdstr_hash_table_entry* entries) {
  grpc_mdstr_hash_table* table = gpr_malloc(sizeof(*table));
  memset(table, 0, sizeof(*table));
  gpr_ref_init(&table->refs, 1);
  table->num_entries = num_entries;
  // Quadratic probing gets best performance when the table is no more
  // than half full.
  table->size = num_entries * 2;
  const size_t entry_size = sizeof(grpc_mdstr_hash_table_entry) * table->size;
  table->entries = gpr_malloc(entry_size);
  memset(table->entries, 0, entry_size);
  for (size_t i = 0; i < num_entries; ++i) {
    grpc_mdstr_hash_table_entry* entry = &entries[i];
    grpc_mdstr_hash_table_add(table, entry->key, entry->value, entry->vtable);
  }
  return table;
}

grpc_mdstr_hash_table* grpc_mdstr_hash_table_ref(grpc_mdstr_hash_table* table) {
  if (table != NULL) gpr_ref(&table->refs);
  return table;
}

int grpc_mdstr_hash_table_unref(grpc_mdstr_hash_table* table) {
  if (table != NULL && gpr_unref(&table->refs)) {
    for (size_t i = 0; i < table->size; ++i) {
      grpc_mdstr_hash_table_entry* entry = &table->entries[i];
      if (entry->key != NULL) {
        GRPC_MDSTR_UNREF(entry->key);
        entry->vtable->destroy_value(entry->value);
      }
    }
    gpr_free(table->entries);
    gpr_free(table);
    return 1;
  }
  return 0;
}

size_t grpc_mdstr_hash_table_num_entries(const grpc_mdstr_hash_table* table) {
  return table->num_entries;
}

void* grpc_mdstr_hash_table_get(const grpc_mdstr_hash_table* table,
                                const grpc_mdstr* key) {
  const size_t idx =
      grpc_mdstr_hash_table_find_index(table, key, false /* find_empty */);
  if (idx == table->size) return NULL;  // Not found.
  return table->entries[idx].value;
}

int grpc_mdstr_hash_table_cmp(const grpc_mdstr_hash_table* table1,
                              const grpc_mdstr_hash_table* table2) {
  // Compare by num_entries.
  if (table1->num_entries < table2->num_entries) return -1;
  if (table1->num_entries > table2->num_entries) return 1;
  for (size_t i = 0; i < table1->num_entries; ++i) {
    grpc_mdstr_hash_table_entry* e1 = &table1->entries[i];
    grpc_mdstr_hash_table_entry* e2 = &table2->entries[i];
    // Compare keys by hash value.
    if (e1->key->hash < e2->key->hash) return -1;
    if (e1->key->hash > e2->key->hash) return 1;
    // Compare by vtable (pointer equality).
    if (e1->vtable < e2->vtable) return -1;
    if (e1->vtable > e2->vtable) return 1;
    // Compare values via vtable.
    const int value_result = e1->vtable->compare_value(e1->value, e2->value);
    if (value_result != 0) return value_result;
  }
  return 0;
}

void grpc_mdstr_hash_table_iterate(
    const grpc_mdstr_hash_table* table,
    void (*func)(const grpc_mdstr_hash_table_entry* entry, void* user_data),
    void* user_data) {
  for (size_t i = 0; i < table->size; ++i) {
    if (table->entries[i].key != NULL) {
      func(&table->entries[i], user_data);
    }
  }
}
