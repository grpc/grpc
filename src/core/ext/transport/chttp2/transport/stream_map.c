/*
 *
 * Copyright 2015 gRPC authors.
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
 *
 */

#include "src/core/ext/transport/chttp2/transport/stream_map.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

void grpc_chttp2_stream_map_init(grpc_chttp2_stream_map *map,
                                 size_t initial_capacity) {
  GPR_ASSERT(initial_capacity > 1);
  map->keys = gpr_malloc(sizeof(uint32_t) * initial_capacity);
  map->values = gpr_malloc(sizeof(void *) * initial_capacity);
  map->count = 0;
  map->free = 0;
  map->capacity = initial_capacity;
}

void grpc_chttp2_stream_map_destroy(grpc_chttp2_stream_map *map) {
  gpr_free(map->keys);
  gpr_free(map->values);
}

static size_t compact(uint32_t *keys, void **values, size_t count) {
  size_t i, out;

  for (i = 0, out = 0; i < count; i++) {
    if (values[i]) {
      keys[out] = keys[i];
      values[out] = values[i];
      out++;
    }
  }

  return out;
}

void grpc_chttp2_stream_map_add(grpc_chttp2_stream_map *map, uint32_t key,
                                void *value) {
  size_t count = map->count;
  size_t capacity = map->capacity;
  uint32_t *keys = map->keys;
  void **values = map->values;

  GPR_ASSERT(count == 0 || keys[count - 1] < key);
  GPR_ASSERT(value);
  GPR_ASSERT(grpc_chttp2_stream_map_find(map, key) == NULL);

  if (count == capacity) {
    if (map->free > capacity / 4) {
      count = compact(keys, values, count);
      map->free = 0;
    } else {
      /* resize when less than 25% of the table is free, because compaction
         won't help much */
      map->capacity = capacity = 3 * capacity / 2;
      map->keys = keys = gpr_realloc(keys, capacity * sizeof(uint32_t));
      map->values = values = gpr_realloc(values, capacity * sizeof(void *));
    }
  }

  keys[count] = key;
  values[count] = value;
  map->count = count + 1;
}

static void **find(grpc_chttp2_stream_map *map, uint32_t key) {
  size_t min_idx = 0;
  size_t max_idx = map->count;
  size_t mid_idx;
  uint32_t *keys = map->keys;
  void **values = map->values;
  uint32_t mid_key;

  if (max_idx == 0) return NULL;

  while (min_idx < max_idx) {
    /* find the midpoint, avoiding overflow */
    mid_idx = min_idx + ((max_idx - min_idx) / 2);
    mid_key = keys[mid_idx];

    if (mid_key < key) {
      min_idx = mid_idx + 1;
    } else if (mid_key > key) {
      max_idx = mid_idx;
    } else /* mid_key == key */
    {
      return &values[mid_idx];
    }
  }

  return NULL;
}

void *grpc_chttp2_stream_map_delete(grpc_chttp2_stream_map *map, uint32_t key) {
  void **pvalue = find(map, key);
  void *out = NULL;
  if (pvalue != NULL) {
    out = *pvalue;
    *pvalue = NULL;
    map->free += (out != NULL);
    /* recognize complete emptyness and ensure we can skip
     * defragmentation later */
    if (map->free == map->count) {
      map->free = map->count = 0;
    }
    GPR_ASSERT(grpc_chttp2_stream_map_find(map, key) == NULL);
  }
  return out;
}

void *grpc_chttp2_stream_map_find(grpc_chttp2_stream_map *map, uint32_t key) {
  void **pvalue = find(map, key);
  return pvalue != NULL ? *pvalue : NULL;
}

size_t grpc_chttp2_stream_map_size(grpc_chttp2_stream_map *map) {
  return map->count - map->free;
}

void *grpc_chttp2_stream_map_rand(grpc_chttp2_stream_map *map) {
  if (map->count == map->free) {
    return NULL;
  }
  if (map->free != 0) {
    map->count = compact(map->keys, map->values, map->count);
    map->free = 0;
  }
  return map->values[((size_t)rand()) % map->count];
}

void grpc_chttp2_stream_map_for_each(grpc_chttp2_stream_map *map,
                                     void (*f)(void *user_data, uint32_t key,
                                               void *value),
                                     void *user_data) {
  size_t i;

  for (i = 0; i < map->count; i++) {
    if (map->values[i]) {
      f(user_data, map->keys[i], map->values[i]);
    }
  }
}
