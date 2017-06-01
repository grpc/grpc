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

#include "src/core/ext/census/intrusive_hash_map_internal.h"

/* intrusive_hash_map is a fast chained hash table. This hash map is faster than
 * a dense hash map when the application calls insert and erase more often than
 * find. When the workload is dominated by find() a dense hash map may be
 * faster.
 *
 * intrusive_hash_map uses an intrusive header placed within a user defined
 * struct. The header field IHM_key MUST be set to a valid value before
 * insertion into the hash map or undefined behavior may occur. The header field
 * IHM_hash_link MUST to be set to NULL initially.
 *
 * EXAMPLE USAGE:
 *
 *  typedef struct string_item {
 *    INTRUSIVE_HASH_MAP_HEADER;
 *    // User data.
 *    char *str_buf;
 *    uint16_t len;
 *  } string_item;
 *
 *  static string_item *make_string_item(uint64_t key, const char *buf,
 *                                       uint16_t len) {
 *    string_item *item = (string_item *)gpr_malloc(sizeof(string_item));
 *    item->IHM_key = key;
 *    item->IHM_hash_link = NULL;
 *    item->len = len;
 *    item->str_buf = (char *)malloc(len);
 *    memcpy(item->str_buf, buf, len);
 *    return item;
 *  }
 *
 *  intrusive_hash_map hash_map;
 *  intrusive_hash_map_init(&hash_map, 4);
 *  string_item *new_item1 = make_string_item(10, "test1", 5);
 *  bool ok = intrusive_hash_map_insert(&hash_map, (hm_item *)new_item1);
 *
 *  string_item *item1 =
 *    (string_item *)intrusive_hash_map_find(&hash_map, 10);
 */

/* Hash map item. Stores key and a pointer to the actual object. A user defined
 * version of this can be passed in provided the first 2 entries (key and
 * hash_link) are the same. These entries must be first in the user defined
 * struct. Pointer to struct will need to be cast as (hm_item *) when passed to
 * hash map. This allows it to be intrusive. */
typedef struct hm_item {
  uint64_t key;
  struct hm_item *hash_link;
  /* Optional user defined data after this. */
} hm_item;

/* Macro provided for ease of use.  This must be first in the user defined
 * struct (i.e. uint64_t key and hm_item * must be the first two elements in
 * that order). */
#define INTRUSIVE_HASH_MAP_HEADER \
  uint64_t IHM_key;               \
  struct hm_item *IHM_hash_link

/* Index struct which acts as a pseudo-iterator within the hash map. */
typedef struct hm_index {
  uint32_t bucket_index;  // hash map bucket index.
  hm_item *item;          // Pointer to hm_item within the hash map.
} hm_index;

/* Returns true if two hm_indices point to the same object within the hash map
 * and false otherwise. */
__inline bool hm_index_compare(const hm_index *A, const hm_index *B) {
  return (A->item == B->item && A->bucket_index == B->bucket_index);
}

/*
 * Helper functions for iterating over the hash map.
 */

/* On return idx will contain an invalid index which is always equal to
 * hash_map->buckets.size_ */
void intrusive_hash_map_end(const intrusive_hash_map *hash_map, hm_index *idx);

/* Iterates index to the next valid entry in the hash map and stores the
 * index within idx. If end of table is reached, idx will contain the same
 * values as if intrusive_hash_map_end() was called. */
void intrusive_hash_map_next(const intrusive_hash_map *hash_map, hm_index *idx);

/* On return, idx will contain the index of the first non-null entry in the hash
 * map. If the hash map is empty, idx will contain the same values as if
 * intrusive_hash_map_end() was called. */
void intrusive_hash_map_begin(const intrusive_hash_map *hash_map,
                              hm_index *idx);

/* Initialize intrusive hash map data structure. This must be called before
 * the hash map can be used. The initial size of an intrusive hash map will be
 * 2^initial_log2_map_size (valid range is [0, 31]). */
void intrusive_hash_map_init(intrusive_hash_map *hash_map,
                             uint32_t initial_log2_map_size);

/* Returns true if the hash map is empty and false otherwise. */
bool intrusive_hash_map_empty(const intrusive_hash_map *hash_map);

/* Returns the number of elements currently in the hash map. */
size_t intrusive_hash_map_size(const intrusive_hash_map *hash_map);

/* Find a hm_item within the hash map by key. Returns NULL if item was not
 * found. */
hm_item *intrusive_hash_map_find(const intrusive_hash_map *hash_map,
                                 uint64_t key);

/* Erase the hm_item that corresponds with key. If the hm_item is found, return
 * the pointer to the hm_item. Else returns NULL. */
hm_item *intrusive_hash_map_erase(intrusive_hash_map *hash_map, uint64_t key);

/* Attempts to insert a new hm_item into the hash map.  If an element with the
 * same key already exists, it will not insert the new item and return false.
 * Otherwise, it will insert the new item and return true. */
bool intrusive_hash_map_insert(intrusive_hash_map *hash_map, hm_item *item);

/* Clears entire contents of the hash map, but leaves internal data structure
 * untouched. Second argument takes a function pointer to a function that will
 * free the object designated by the user and pointed to by hash_map->value. */
void intrusive_hash_map_clear(intrusive_hash_map *hash_map,
                              void (*free_object)(void *));

/* Erase all contents of hash map and free the memory. Hash map is invalid
 * after calling this function and cannot be used until it has been
 * reinitialized (intrusive_hash_map_init()). This function takes a function
 * pointer to a function that will free the object designated by the user and
 * pointed to by hash_map->value. */
void intrusive_hash_map_free(intrusive_hash_map *hash_map,
                             void (*free_object)(void *));

#endif /* GRPC_CORE_EXT_CENSUS_INTRUSIVE_HASH_MAP_H */
