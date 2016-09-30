/*
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef GRPC_CORE_LIB_TRANSPORT_HASHTABLE_H
#define GRPC_CORE_LIB_TRANSPORT_HASHTABLE_H

#include "src/core/lib/transport/metadata.h"

/** Hash table implementation.
 *
 * This implementation uses open addressing
 * (https://en.wikipedia.org/wiki/Open_addressing) with quadratic
 * probing (https://en.wikipedia.org/wiki/Quadratic_probing).
 * This means that the hash table is of fixed size and cannot contain
 * more than that number of elements.
 *
 * The keys are grpc_mdstr objects.  The values are arbitrary pointers
 * with a common vtable.
 *
 * Hash tables are intentionally immutable, to avoid the need for locking.
 */

typedef struct grpc_hash_table grpc_hash_table;

typedef struct grpc_hash_table_vtable {
  void (*destroy_value)(void* value);
  void* (*copy_value)(void* value);
  int (*compare_value)(void* value1, void* value2);
} grpc_hash_table_vtable;

typedef struct grpc_hash_table_entry {
  grpc_mdstr* key;
  void* value; /* Must not be NULL. */
  const grpc_hash_table_vtable* vtable;
} grpc_hash_table_entry;

/** Creates a new hash table of containing \a entries, which is an array
    of length \a num_entries.
    Creates its own copy of all keys and values from \a entries. */
grpc_hash_table* grpc_hash_table_create(size_t num_entries,
                                        grpc_hash_table_entry* entries);

grpc_hash_table* grpc_hash_table_ref(grpc_hash_table* table);
/** Returns 1 when \a table is destroyed. */
int grpc_hash_table_unref(grpc_hash_table* table);

/** Returns the value from \a table associated with \a key.
    Returns NULL if \a key is not found. */
void* grpc_hash_table_get(grpc_hash_table* table, grpc_mdstr* key);

/** Compares two hash tables. */
int grpc_hash_table_cmp(grpc_hash_table* table1, grpc_hash_table* table2);

#endif /* GRPC_CORE_LIB_TRANSPORT_HASHTABLE_H */
