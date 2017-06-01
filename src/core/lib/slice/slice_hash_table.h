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
  void *value; /* Must not be NULL. */
} grpc_slice_hash_table_entry;

/** Creates a new hash table of containing \a entries, which is an array
    of length \a num_entries.  Takes ownership of all keys and values in \a
    entries.  Values will be cleaned up via \a destroy_value(). If not NULL, \a
    value_cmp will be used to compare values in the context of \a
    grpc_slice_hash_table_cmp. If NULL, raw pointer (\a GPR_ICMP) comparison
    will be used. */
grpc_slice_hash_table *grpc_slice_hash_table_create(
    size_t num_entries, grpc_slice_hash_table_entry *entries,
    void (*destroy_value)(grpc_exec_ctx *exec_ctx, void *value),
    int (*value_cmp)(void *a, void *b));

grpc_slice_hash_table *grpc_slice_hash_table_ref(grpc_slice_hash_table *table);
void grpc_slice_hash_table_unref(grpc_exec_ctx *exec_ctx,
                                 grpc_slice_hash_table *table);

/** Returns the value from \a table associated with \a key.
    Returns NULL if \a key is not found. */
void *grpc_slice_hash_table_get(const grpc_slice_hash_table *table,
                                const grpc_slice key);

/** Compares \a a vs. \a b.
 * A table is considered "smaller" (resp. "greater") if:
 *  - GPR_ICMP(a->value_cmp, b->value_cmp) < 1 (resp. > 1),
 *  - else, it contains fewer (resp. more) entries,
 *  - else, if (a_key, a_value) < (b_key, b_value) (resp >), which is defined as
 *    strcmp(a_key, b_key) < 1 (resp. > 1),
 *  - else, if value_cmp(a_value, b_value) < 1 (resp. > 1). */
int grpc_slice_hash_table_cmp(const grpc_slice_hash_table *a,
                              const grpc_slice_hash_table *b);

#endif /* GRPC_CORE_LIB_SLICE_SLICE_HASH_TABLE_H */
