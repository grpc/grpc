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

#include "src/core/ext/census/intrusive_hash_map.h"
#include <string.h>

extern bool hm_index_compare(const hm_index *A, const hm_index *B);

/* Simple hashing function that takes lower 32 bits. */
static __inline uint32_t chunked_vector_hasher(uint64_t key) {
  return (uint32_t)key;
}

/* Vector chunks are 1MiB divided by pointer size. */
static const size_t VECTOR_CHUNK_SIZE = (1 << 20) / sizeof(void *);

/* Helper functions which return buckets from the chunked vector. */
static __inline void **get_mutable_bucket(const chunked_vector *buckets,
                                          uint32_t index) {
  if (index < VECTOR_CHUNK_SIZE) {
    return &buckets->first_[index];
  }
  size_t rest_index = (index - VECTOR_CHUNK_SIZE) / VECTOR_CHUNK_SIZE;
  return &buckets->rest_[rest_index][index % VECTOR_CHUNK_SIZE];
}

static __inline void *get_bucket(const chunked_vector *buckets,
                                 uint32_t index) {
  if (index < VECTOR_CHUNK_SIZE) {
    return buckets->first_[index];
  }
  size_t rest_index = (index - VECTOR_CHUNK_SIZE) / VECTOR_CHUNK_SIZE;
  return buckets->rest_[rest_index][index % VECTOR_CHUNK_SIZE];
}

/* Helper function. */
static __inline size_t RestSize(const chunked_vector *vec) {
  return (vec->size_ <= VECTOR_CHUNK_SIZE)
             ? 0
             : (vec->size_ - VECTOR_CHUNK_SIZE - 1) / VECTOR_CHUNK_SIZE + 1;
}

/* Initialize chunked vector to size of 0. */
static void chunked_vector_init(chunked_vector *vec) {
  vec->size_ = 0;
  vec->first_ = NULL;
  vec->rest_ = NULL;
}

/* Clear chunked vector and free all memory that has been allocated then
   initialize chunked vector. */
static void chunked_vector_clear(chunked_vector *vec) {
  if (vec->first_ != NULL) {
    gpr_free(vec->first_);
  }
  if (vec->rest_ != NULL) {
    size_t rest_size = RestSize(vec);
    for (size_t i = 0; i < rest_size; ++i) {
      if (vec->rest_[i] != NULL) {
        gpr_free(vec->rest_[i]);
      }
    }
    gpr_free(vec->rest_);
  }
  chunked_vector_init(vec);
}

/* Clear chunked vector and then resize it to n entries. Allow the first 1MB to
   be read w/o an extra cache miss. The rest of the elements are stored in an
   array of arrays to avoid large mallocs. */
static void chunked_vector_reset(chunked_vector *vec, size_t n) {
  chunked_vector_clear(vec);
  vec->size_ = n;
  if (n <= VECTOR_CHUNK_SIZE) {
    vec->first_ = (void **)gpr_malloc(sizeof(void *) * n);
    memset(vec->first_, 0, sizeof(void *) * n);
  } else {
    vec->first_ = (void **)gpr_malloc(sizeof(void *) * VECTOR_CHUNK_SIZE);
    memset(vec->first_, 0, sizeof(void *) * VECTOR_CHUNK_SIZE);
    size_t rest_size = RestSize(vec);
    vec->rest_ = (void ***)gpr_malloc(sizeof(void **) * rest_size);
    memset(vec->rest_, 0, sizeof(void **) * rest_size);
    int i = 0;
    n -= VECTOR_CHUNK_SIZE;
    while (n > 0) {
      size_t this_size = GPR_MIN(n, VECTOR_CHUNK_SIZE);
      vec->rest_[i] = (void **)gpr_malloc(sizeof(void *) * this_size);
      memset(vec->rest_[i], 0, sizeof(void *) * this_size);
      n -= this_size;
      ++i;
    }
  }
}

void intrusive_hash_map_init(intrusive_hash_map *hash_map,
                             uint32_t initial_log2_table_size) {
  hash_map->log2_num_buckets = initial_log2_table_size;
  hash_map->num_items = 0;
  uint32_t num_buckets = (uint32_t)1 << hash_map->log2_num_buckets;
  hash_map->extend_threshold = num_buckets >> 1;
  chunked_vector_init(&hash_map->buckets);
  chunked_vector_reset(&hash_map->buckets, num_buckets);
  hash_map->hash_mask = num_buckets - 1;
}

bool intrusive_hash_map_empty(const intrusive_hash_map *hash_map) {
  return hash_map->num_items == 0;
}

size_t intrusive_hash_map_size(const intrusive_hash_map *hash_map) {
  return hash_map->num_items;
}

void intrusive_hash_map_end(const intrusive_hash_map *hash_map, hm_index *idx) {
  idx->bucket_index = (uint32_t)hash_map->buckets.size_;
  GPR_ASSERT(idx->bucket_index <= UINT32_MAX);
  idx->item = NULL;
}

void intrusive_hash_map_next(const intrusive_hash_map *hash_map,
                             hm_index *idx) {
  idx->item = idx->item->hash_link;
  while (idx->item == NULL) {
    idx->bucket_index++;
    if (idx->bucket_index >= hash_map->buckets.size_) {
      /* Reached end of table. */
      idx->item = NULL;
      return;
    }
    idx->item = (hm_item *)get_bucket(&hash_map->buckets, idx->bucket_index);
  }
}

void intrusive_hash_map_begin(const intrusive_hash_map *hash_map,
                              hm_index *idx) {
  for (uint32_t i = 0; i < hash_map->buckets.size_; ++i) {
    if (get_bucket(&hash_map->buckets, i) != NULL) {
      idx->bucket_index = i;
      idx->item = (hm_item *)get_bucket(&hash_map->buckets, i);
      return;
    }
  }
  intrusive_hash_map_end(hash_map, idx);
}

hm_item *intrusive_hash_map_find(const intrusive_hash_map *hash_map,
                                 uint64_t key) {
  uint32_t index = chunked_vector_hasher(key) & hash_map->hash_mask;

  hm_item *p = (hm_item *)get_bucket(&hash_map->buckets, index);
  while (p != NULL) {
    if (key == p->key) {
      return p;
    }
    p = p->hash_link;
  }
  return NULL;
}

hm_item *intrusive_hash_map_erase(intrusive_hash_map *hash_map, uint64_t key) {
  uint32_t index = chunked_vector_hasher(key) & hash_map->hash_mask;

  hm_item **slot = (hm_item **)get_mutable_bucket(&hash_map->buckets, index);
  hm_item *p = *slot;
  if (p == NULL) {
    return NULL;
  }

  if (key == p->key) {
    *slot = p->hash_link;
    p->hash_link = NULL;
    hash_map->num_items--;
    return p;
  }

  hm_item *prev = p;
  p = p->hash_link;

  while (p) {
    if (key == p->key) {
      prev->hash_link = p->hash_link;
      p->hash_link = NULL;
      hash_map->num_items--;
      return p;
    }
    prev = p;
    p = p->hash_link;
  }
  return NULL;
}

/* Insert an hm_item* into the underlying chunked vector. hash_mask is
 * array_size-1. Returns true if it is a new hm_item and false if the hm_item
 * already existed.
 */
static __inline bool intrusive_hash_map_internal_insert(chunked_vector *buckets,
                                                        uint32_t hash_mask,
                                                        hm_item *item) {
  const uint64_t key = item->key;
  uint32_t index = chunked_vector_hasher(key) & hash_mask;
  hm_item **slot = (hm_item **)get_mutable_bucket(buckets, index);
  hm_item *p = *slot;
  item->hash_link = p;

  /* Check to see if key already exists. */
  while (p) {
    if (p->key == key) {
      return false;
    }
    p = p->hash_link;
  }

  /* Otherwise add new entry. */
  *slot = item;
  return true;
}

/* Extend the allocated number of elements in the hash map by a factor of 2. */
void intrusive_hash_map_extend(intrusive_hash_map *hash_map) {
  uint32_t new_log2_num_buckets = 1 + hash_map->log2_num_buckets;
  uint32_t new_num_buckets = (uint32_t)1 << new_log2_num_buckets;
  GPR_ASSERT(new_num_buckets <= UINT32_MAX && new_num_buckets > 0);
  chunked_vector new_buckets;
  chunked_vector_init(&new_buckets);
  chunked_vector_reset(&new_buckets, new_num_buckets);
  uint32_t new_hash_mask = new_num_buckets - 1;

  hm_index cur_idx;
  hm_index end_idx;
  intrusive_hash_map_end(hash_map, &end_idx);
  intrusive_hash_map_begin(hash_map, &cur_idx);
  while (!hm_index_compare(&cur_idx, &end_idx)) {
    hm_item *new_item = cur_idx.item;
    intrusive_hash_map_next(hash_map, &cur_idx);
    intrusive_hash_map_internal_insert(&new_buckets, new_hash_mask, new_item);
  }

  /* Set values for new chunked_vector. extend_threshold is set to half of
   * new_num_buckets. */
  hash_map->log2_num_buckets = new_log2_num_buckets;
  chunked_vector_clear(&hash_map->buckets);
  hash_map->buckets = new_buckets;
  hash_map->hash_mask = new_hash_mask;
  hash_map->extend_threshold = new_num_buckets >> 1;
}

/* Insert a hm_item. The hm_item must remain live until it is removed from the
   table. This object does not take the ownership of hm_item. The caller must
   remove this hm_item from the table and delete it before this table is
   deleted. If hm_item exists already num_items is not changed. */
bool intrusive_hash_map_insert(intrusive_hash_map *hash_map, hm_item *item) {
  if (hash_map->num_items >= hash_map->extend_threshold) {
    intrusive_hash_map_extend(hash_map);
  }
  if (intrusive_hash_map_internal_insert(&hash_map->buckets,
                                         hash_map->hash_mask, item)) {
    hash_map->num_items++;
    return true;
  }
  return false;
}

void intrusive_hash_map_clear(intrusive_hash_map *hash_map,
                              void (*free_object)(void *)) {
  hm_index cur;
  hm_index end;
  intrusive_hash_map_end(hash_map, &end);
  intrusive_hash_map_begin(hash_map, &cur);

  while (!hm_index_compare(&cur, &end)) {
    hm_index next = cur;
    intrusive_hash_map_next(hash_map, &next);
    if (cur.item != NULL) {
      hm_item *item = intrusive_hash_map_erase(hash_map, cur.item->key);
      (*free_object)((void *)item);
      gpr_free(item);
    }
    cur = next;
  }
}

void intrusive_hash_map_free(intrusive_hash_map *hash_map,
                             void (*free_object)(void *)) {
  intrusive_hash_map_clear(hash_map, (*free_object));
  hash_map->num_items = 0;
  hash_map->extend_threshold = 0;
  hash_map->log2_num_buckets = 0;
  hash_map->hash_mask = 0;
  chunked_vector_clear(&hash_map->buckets);
}
