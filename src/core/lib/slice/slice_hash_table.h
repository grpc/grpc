/*
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GRPC_CORE_LIB_SLICE_SLICE_HASH_TABLE_H
#define GRPC_CORE_LIB_SLICE_SLICE_HASH_TABLE_H

#include "src/core/lib/transport/metadata.h"

/** Hash table implementation.
 *
 * This implementation uses open addressing
 * (https://en.wikipedia.org/wiki/Open_addressing) with quadratic
 * probing (https://en.wikipedia.org/wiki/Quadratic_probing).
 *
 * The keys are \a grpc_slice objects.  The values are arbitrary pointers
 * with a common vtable.
 *
 * Hash tables are intentionally immutable, to avoid the need for locking.
 */

typedef struct grpc_slice_hash_table grpc_slice_hash_table;

typedef struct grpc_slice_hash_table_vtable {
  void (*destroy_value)(grpc_exec_ctx *exec_ctx, void *value);
  void *(*copy_value)(void *value);
} grpc_slice_hash_table_vtable;

typedef struct grpc_slice_hash_table_entry {
  grpc_slice key;
  void *value; /* Must not be NULL. */
  const grpc_slice_hash_table_vtable *vtable;
} grpc_slice_hash_table_entry;

/** Creates a new hash table of containing \a entries, which is an array
    of length \a num_entries.
    Creates its own copy of all keys and values from \a entries. */
grpc_slice_hash_table *grpc_slice_hash_table_create(
    size_t num_entries, grpc_slice_hash_table_entry *entries);

grpc_slice_hash_table *grpc_slice_hash_table_ref(grpc_slice_hash_table *table);
void grpc_slice_hash_table_unref(grpc_exec_ctx *exec_ctx,
                                 grpc_slice_hash_table *table);

/** Returns the value from \a table associated with \a key.
    Returns NULL if \a key is not found. */
void *grpc_slice_hash_table_get(const grpc_slice_hash_table *table,
                                const grpc_slice key);

#endif /* GRPC_CORE_LIB_SLICE_SLICE_HASH_TABLE_H */
