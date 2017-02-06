/*
 *
 * Copyright 2017, Google Inc.
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
 *
 */

#ifndef GRPC_CORE_EXT_CENSUS_INTRUSIVE_HASH_MAP_H
#define GRPC_CORE_EXT_CENSUS_INTRUSIVE_HASH_MAP_H

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>
#include <stdbool.h>

/* The hash array is doubled if the number of items in the table exceeds this
   fraction of the table size. */
static const float kIntrusiveLoadFactor = 0.5;

/* The initial size of an intrusive hash map will be 2 to this power. */
static const uint32_t kInitialLog2TableSize = 4;

/* Hash table item. */
typedef struct ht_item {
  uint64_t key;
  void *value;
  struct ht_item *hash_link;
} ht_item;

/* Helper function that creates a new hash table item.  It is up to the user to
   free the item that was allocated. */
inline ht_item *make_new_item(uint64_t key, void *value) {
  ht_item *new_item = gpr_malloc(sizeof(ht_item));
  new_item->key = key;
  new_item->value = value;
  new_item->hash_link = NULL;
  return new_item;
}

/* The chunked vector is a data structure that allocates buckets for use in a
   hash table. ChunkedVector is logically equivalent to T*[N] (cast void* as
   type of T*). It's internally implemented as an array of 1MB arrays to avoid
   allocating large consecutive memory chunks. This is an internal data
   structure that should never be accessed directly. */
typedef struct chunked_vector {
  size_t size_;
  void **first_;
  void ***rest_;
} chunked_vector;

typedef struct intrusive_hash_map {
  uint32_t num_items;
  uint32_t extend_threshold;
  uint32_t log2_num_buckets;
  uint32_t hash_mask;
  chunked_vector buckets;
} intrusive_hash_map;

/* Initialize intrusive hash map data structure.  This must be called before
   the hash map can be used. */
void intrusive_hash_map_init(intrusive_hash_map *hash,
                             uint32_t initial_log2_table_size);

bool intrusive_hash_map_empty(const intrusive_hash_map *hash);

size_t intrusive_hash_map_size(const intrusive_hash_map *hash);

/* Find a ht_item within the hash map by key. Returns NULL if item was not
   found. */
ht_item *intrusive_hash_map_find(const intrusive_hash_map *hash, uint64_t key);

/* Erase the ht_item for @p key. If the ht_item is found, return the pointer to
   the ht_item. Else return a null pointer. */
ht_item *intrusive_hash_map_erase(intrusive_hash_map *hash, uint64_t key);

bool intrusive_hash_map_insert(intrusive_hash_map *hash, uint64_t key,
                               ht_item *new_item);

/* Clear entire contents of hash map. */
void intrusive_hash_map_clear(intrusive_hash_map *hash);

/* Erase all contents of hash map and free the memory. Hash table is invalid
   after calling this function and cannot be used until it has been
   reinitialized (intrusive_hash_map_init()). */
void intrusive_hash_map_free(intrusive_hash_map *hash);

#endif
