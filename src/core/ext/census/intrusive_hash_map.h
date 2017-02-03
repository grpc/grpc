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

/* The chunked vector is a data structure that allocates buckets for use in a
   hash table. ChunkedVector is logically equivalent to T*[N] (cast void* as
   type of T*). It's internally implemented as an array of 1MB arrays to avoid
   allocating large consecutive memory chunks. */

/* Simple hashing function that takes lower 32 bits. */
inline uint32_t chunked_vector_hasher(uint64_t key) { return (uint32_t)key; }

/* Vector chunks are 1MB divided by pointer size. */
const size_t VECTOR_CHUNK_SIZE = (1 << 20) / sizeof(void *);

/* Initialize chunked vector to size of 0. */
void chunked_vector_init(chunked_vector *vec);

/* Clear chunked vector and free all memory that has been allocated then
   initialize chunked vector. */
void chunked_vector_clear(chunked_vector *vec);

/* Clear chunked vector and then resize it to n entries. */
void chunked_vector_reset(chunked_vector *vec, size_t entry_size, size_t n);

/* Allow the first 1MB to be read w/o an extra cache miss. The rest of the
   elements are stored in an array of arrays to avoid large mallocs. */
typedef struct chunked_vector {
  size_t size_;
  void **first_;
  void ***rest_;
} chunked_vector;

inline void **get_mutable_bucket(const chunked_vector *buckets,
                                 uint32_t index) {
  if (index < VECTOR_CHUNK_SIZE) {
    return &buckets->first_[index];
  }
  uint32_t rest_index = (index - VECTOR_CHUNK_SIZE) / VECTOR_CHUNK_SIZE;
  return &buckets->rest_[rest_index][index % VECTOR_CHUNK_SIZE];
}

inline void *get_bucket(const chunked_vector *buckets, uint32_t index) {
  if (index < VECTOR_CHUNK_SIZE) {
    return buckets->first_[index];
  }
  size_t rest_index = (index - VECTOR_CHUNK_SIZE) / VECTOR_CHUNK_SIZE;
  return buckets->rest_[rest_index][index % VECTOR_CHUNK_SIZE];
}

/* Helper function. */
inline uint32_t RestSize(const chunked_vector *vec) {
  if (vec->size_ <= VECTOR_CHUNK_SIZE) return 0;
  return (vec->size_ - VECTOR_CHUNK_SIZE - 1) / VECTOR_CHUNK_SIZE + 1;
}

/* The hash array is doubled if the number of items in the table exceeds this
   fraction of the table size. */
static const float kIntrusiveLoadFactor = 0.5;

/* The initial size of an intrusive hash map will be 2 to this power. */
static const int kInitialLog2TableSize = 4;

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

typedef struct intrusive_hash_map {
  uint32_t num_items;
  uint32_t extend_threshold;
  uint32_t log2_num_buckets;
  uint32_t hash_mask;
  chunked_vector buckets;
} intrusive_hash_map;

void intrusive_hash_map_init(intrusive_hash_map *hash,
                             uint32_t initial_log2_table_size);

bool intrusive_hash_map_empty(const intrusive_hash_map *hash);

size_t intrusive_hash_map_size(const intrusive_hash_map *hash);

void init_intrusive_hash_map(intrusive_hash_map *hash,
                             uint32_t initial_log2_table_size);

void *init_intrusive_hash_find(intrusive_hash_map *hash, const uint64_t key);

/* Helper functions which allow traversal of hash entries. */
inline uint32_t get_index(ht_item *p) { return hasher_(p->key); }

/* Returns a invalid index which is always equal to hash->buckets.size_ */
void intrusive_hash_map_end(const intrusive_hash_map *hash, uint32_t *index,
                            ht_item **value);

/* Iterates index to the next valid entry in the hash table. */
void intrusive_hash_map_next(const intrusive_hash_map *hash, uint32_t *index,
                             ht_item **value);

/* Returns first non-null entry in hash table.  If hash table is empty this will
   return the same values as end(). */
void intrusive_hash_map_begin(const intrusive_hash_map *hash, uint32_t *index,
                              ht_item **value);

/* Erase the ht_item for @p key. If the ht_item is found, return the pointer to
   the ht_item. Else return a null pointer. */
ht_item *intrusive_hash_map_erase(intrusive_hash_map *hash, const uint64_t key);

void intrusive_hash_map_extend(intrusive_hash_map *hash);

void *intrusive_hash_map_insert(intrusive_hash_map *hash, uint64_t key,
                                ht_item *new_item);

#endif
