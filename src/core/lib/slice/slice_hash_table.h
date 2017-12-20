/*
 * Copyright 2016 gRPC authors.
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
 */

#ifndef GRPC_CORE_LIB_SLICE_SLICE_HASH_TABLE_H
#define GRPC_CORE_LIB_SLICE_SLICE_HASH_TABLE_H

#include "src/core/lib/transport/metadata.h"

/** Hash table implementation.
 *
 * This implementation uses open addressing
 * (https://en.wikipedia.org/wiki/Open_addressing) with linear
 * probing (https://en.wikipedia.org/wiki/Linear_probing).
 *
 * The keys are \a grpc_slice objects.  The values are arbitrary pointers
 * with a common destroy function.
 *
 * Hash tables are intentionally immutable, to avoid the need for locking.
 */

typedef struct grpc_slice_hash_table grpc_slice_hash_table;

typedef struct grpc_slice_hash_table_entry {
  grpc_slice key;
  void* value; /* Must not be NULL. */
} grpc_slice_hash_table_entry;

/** Creates a new hash table of containing \a entries, which is an array
    of length \a num_entries.  Takes ownership of all keys and values in \a
    entries.  Values will be cleaned up via \a destroy_value(). If not NULL, \a
    value_cmp will be used to compare values in the context of \a
    grpc_slice_hash_table_cmp. If NULL, raw pointer (\a GPR_ICMP) comparison
    will be used. */
grpc_slice_hash_table* grpc_slice_hash_table_create(
    size_t num_entries, grpc_slice_hash_table_entry* entries,
    void (*destroy_value)(void* value), int (*value_cmp)(void* a, void* b));

grpc_slice_hash_table* grpc_slice_hash_table_ref(grpc_slice_hash_table* table);
void grpc_slice_hash_table_unref(grpc_slice_hash_table* table);

/** Returns the value from \a table associated with \a key.
    Returns NULL if \a key is not found. */
void* grpc_slice_hash_table_get(const grpc_slice_hash_table* table,
                                const grpc_slice key);

/** Compares \a a vs. \a b.
 * A table is considered "smaller" (resp. "greater") if:
 *  - GPR_ICMP(a->value_cmp, b->value_cmp) < 1 (resp. > 1),
 *  - else, it contains fewer (resp. more) entries,
 *  - else, if strcmp(a_key, b_key) < 1 (resp. > 1),
 *  - else, if value_cmp(a_value, b_value) < 1 (resp. > 1). */
int grpc_slice_hash_table_cmp(const grpc_slice_hash_table* a,
                              const grpc_slice_hash_table* b);

#endif /* GRPC_CORE_LIB_SLICE_SLICE_HASH_TABLE_H */
