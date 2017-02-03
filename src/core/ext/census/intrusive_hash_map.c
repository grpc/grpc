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

void chunked_vector_init(chunked_vector *vec) {
  vec->size_ = 0;
  vec->first_ = NULL;
  vec->rest_ = NULL;
}

void chunked_vector_clear(chunked_vector *vec) {
  gpr_free(vec->first_);
  uint32_t rest_size = RestSize(vec);
  for (uint32_t i = 0; i < rest_size; ++i) {
    gpr_free(vec->rest_[i]);
  }
  gpr_free(vec->rest_);
  chunked_vector_init(vec);
}

void chunked_vector_reset(chunked_vector *vec, size_t entry_size, size_t n) {
  chunked_vector_clear(vec);
  vec->size_ = n;
  if (n <= VECTOR_CHUNK_SIZE) {
    vec->first_ = gpr_malloc(entry_size * n);
    vec->rest_ = NULL;
  } else {
    vec->first_ = (void *)gpr_malloc(entry_size * VECTOR_CHUNK_SIZE);
    uint32_t rest_size = RestSize(vec);
    vec->rest_ = (void *)gpr_malloc(sizeof(void **) * rest_size);
    int i = 0;
    n -= VECTOR_CHUNK_SIZE;
    while (n > 0) {
      size_t this_size = GPR_MIN(n, VECTOR_CHUNK_SIZE);
      vec->rest_[i] = (void *)gpr_malloc(entry_size * this_size);
      n -= this_size;
      ++i;
    }
  }
}

void intrusive_hash_map_init(intrusive_hash_map *hash,
                             uint32_t initial_log2_table_size) {
  hash->log2_num_buckets = initial_log2_table_size;
  hash->num_items = 0;
  uint32_t num_buckets = (uint32_t)1 << hash->log2_num_buckets;
  hash->extend_threshold =
      (uint32_t)((float)num_buckets * kIntrusiveLoadFactor);
  chunked_vector_reset(&hash->buckets, sizeof(item *), num_buckets);
  hash->hash_mask = num_buckets - 1;
}

bool intrusive_hash_map_empty(const intrusive_hash_map *hash) {
  return hash->num_items == 0;
}

size_t intrusive_hash_map_size(const intrusive_hash_map *hash) {
  return hash->num_items;
}

void *intrusive_hash_map_find(intrusive_hash_map *hash, const uint64_t key) {
  uint32_t index = hasher_(key) & hash->hash_mask;

  item *p = (item *)get_bucket(&hash->buckets, index);
  while (p != NULL) {
    if (key == p->key) {
      return p->value;
    }
    p = p->hash_link;
  }
  return NULL;
}

/* Returns a invalid index which is always equal to hash->buckets.size_ */
void intrusive_hash_map_end(const intrusive_hash_map *hash, uint32_t *index,
                            item **value) {
  *index = (uint32_t)hash->buckets.size_;
  *value = NULL;
}

/* Iterates index to the next valid entry in the hash table. */
void intrusive_hash_map_next(const intrusive_hash_map *hash, uint32_t *index,
                             item **value) {
  *value = (*value)->hash_link;
  while (*value == NULL) {
    (*index)++;
    if (*index >= hash->buckets.size_) {
      *value = NULL;
      return;
    }
    *value = (item *)get_bucket(&hash->buckets, *index);
  }
}

/* Returns first non-null entry in hash table.  If hash table is empty this will
   return the same values as end(). */
void intrusive_hash_map_begin(const intrusive_hash_map *hash, uint32_t *index,
                              item **value) {
  for (uint32_t i = 0; i < hash->buckets.size_; ++i) {
    if (get_bucket(&hash->buckets, i) != NULL) {
      *index = i;
      *value = (item *)get_bucket(&hash->buckets, i);
      return;
    }
  }
  end(hash, index, value);
}

/* Erase the item for @p key. If the item is found, return the pointer to the
   item. Else return a null pointer. */
item *intrusive_hash_map_erase(intrusive_hash_map *hash, const uint64_t key) {
  uint32_t index = hasher_(key) & hash->hash_mask;

  item **slot = (item **)get_mutable_bucket(&hash->buckets, index);
  item *p = *slot;
  if (p == NULL) {
    return NULL;
  }

  if (key == p->key) {
    *slot = p->hash_link;
    hash->num_items--;
    return p;
  }

  item *prev = p;
  p = p->hash_link;

  while (p) {
    if (key == p->key) {
      prev->hash_link = p->hash_link;
      hash->num_items--;
      return p;
    }
    prev = p;
    p = p->hash_link;
  }
  return NULL;
}

/* Insert an item into a hash array. hash_mask is array_size-1.
   Returns true if it is a new item and false if the item already existed. */
static inline bool intrusive_hash_map_internal_insert(chunked_vector *buckets,
                                                      uint32_t hash_mask,
                                                      item *new_item) {
  const uint64_t key = new_item->key;
  uint32_t index = hasher_(key) & hash_mask;
  item **slot = (item **)get_mutable_bucket(buckets, index);
  item *p = *slot;
  new_item->hash_link = p;

  /* Check to see if key already exists. */
  while (p) {
    if (p->key == key) {
      return false;
    }
    p = p->hash_link;
  }

  /* Otherwise add new entry. */
  *slot = new_item;
  return true;
}

/* Extend the allocated number of elements in the hash map by a factor of 2. */
void intrusive_hash_map_extend(intrusive_hash_map *hash) {
  uint32_t new_log2_num_buckets = 1 + hash->log2_num_buckets;
  uint32_t new_num_buckets = (uint32_t)1 << new_log2_num_buckets;
  chunked_vector new_buckets;
  init_chunked_vector(&new_buckets);
  chunked_vector_reset(&new_buckets, sizeof(item *), new_num_buckets);
  uint32_t new_hash_mask = new_num_buckets - 1;

  item *value;
  item *end_value;
  uint32_t index;
  uint32_t end_index;
  end(hash, &end_index, &end_value);
  begin(hash, &index, &value);
  while (!(index == end_index && value == end_value)) {
    item *new_value = value;
    next(hash, &index, &value);
    internal_insert(&new_buckets, new_hash_mask, new_value);
  }

  /* Set values for new chunked_vector. */
  hash->log2_num_buckets = new_log2_num_buckets;
  chunked_vector_clear(&hash->buckets);
  hash->buckets = new_buckets;
  hash->hash_mask = new_hash_mask;
  hash->extend_threshold =
      (uint32_t)((float)new_num_buckets * kIntrusiveLoadFactor);
}

/* Insert an item. @p item must remain live until it is removed from the table.
   This object does not take the ownership of @p item. The caller must remove
   this @p item from the table and delete it before this table is deleted. If
   item exists already num_items is not changed. */
void *intrusive_hash_map_insert(intrusive_hash_map *hash, uint64_t key, item *new_item) {
  if (hash->num_items >= hash->extend_threshold) {
    intrusive_hash_map_extend(hash);
  }
  if (intrusive_hash_map_internal_insert(&hash->buckets, hash->hash_mask,
                                         new_item)) {
    hash->num_items++;
  }
  return new_item;
}
