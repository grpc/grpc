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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/stream_map.h"

#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

void grpc_chttp2_stream_map_init(grpc_chttp2_stream_map* map,
                                 size_t initial_capacity) {
  GPR_DEBUG_ASSERT(initial_capacity > 1);
  map->keys =
      static_cast<uint32_t*>(gpr_malloc(sizeof(uint32_t) * initial_capacity));
  map->values =
      static_cast<void**>(gpr_malloc(sizeof(void*) * initial_capacity));
  map->count = 0;
  map->free = 0;
  map->capacity = initial_capacity;
}

void grpc_chttp2_stream_map_destroy(grpc_chttp2_stream_map* map) {
  gpr_free(map->keys);
  gpr_free(map->values);
}

static size_t compact(uint32_t* keys, void** values, size_t count) {
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

void grpc_chttp2_stream_map_add(grpc_chttp2_stream_map* map, uint32_t key,
                                void* value) {
  size_t count = map->count;
  size_t capacity = map->capacity;
  uint32_t* keys = map->keys;
  void** values = map->values;

  // The first assertion ensures that the table is monotonically increasing.
  GPR_ASSERT(count == 0 || keys[count - 1] < key);
  GPR_DEBUG_ASSERT(value);
  // Asserting that the key is not already in the map can be a debug assertion.
  // Why: we're already checking that the map elements are monotonically
  // increasing. If we re-add a key, i.e. if the key is already present, then
  // either it is the most recently added key in the map (in which case the
  // first assertion fails due to key == last_key) or there is a more recently
  // added (larger) key at the end of the map: in which case the first assertion
  // still fails due to key < last_key.
  GPR_DEBUG_ASSERT(grpc_chttp2_stream_map_find(map, key) == nullptr);

  if (count == capacity) {
    if (map->free > capacity / 4) {
      count = compact(keys, values, count);
      map->free = 0;
    } else {
      /* resize when less than 25% of the table is free, because compaction
         won't help much */
      map->capacity = capacity = 2 * capacity;
      map->keys = keys = static_cast<uint32_t*>(
          gpr_realloc(keys, capacity * sizeof(uint32_t)));
      map->values = values =
          static_cast<void**>(gpr_realloc(values, capacity * sizeof(void*)));
    }
  }

  keys[count] = key;
  values[count] = value;
  map->count = count + 1;
}

template <bool strict_find>
static void** find(grpc_chttp2_stream_map* map, uint32_t key) {
  size_t min_idx = 0;
  size_t max_idx = map->count;
  size_t mid_idx;
  uint32_t* keys = map->keys;
  void** values = map->values;
  uint32_t mid_key;

  GPR_DEBUG_ASSERT(!strict_find || max_idx > 0);
  if (!strict_find && max_idx == 0) return nullptr;

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

  GPR_DEBUG_ASSERT(!strict_find);
  return nullptr;
}

void* grpc_chttp2_stream_map_delete(grpc_chttp2_stream_map* map, uint32_t key) {
  void** pvalue = find<true>(map, key);
  GPR_DEBUG_ASSERT(pvalue != nullptr);
  void* out = *pvalue;
  GPR_DEBUG_ASSERT(out != nullptr);
  *pvalue = nullptr;
  map->free++;
  /* recognize complete emptyness and ensure we can skip
     defragmentation later */
  if (map->free == map->count) {
    map->free = map->count = 0;
  }
  GPR_DEBUG_ASSERT(grpc_chttp2_stream_map_find(map, key) == nullptr);
  return out;
}

void* grpc_chttp2_stream_map_find(grpc_chttp2_stream_map* map, uint32_t key) {
  void** pvalue = find<false>(map, key);
  return pvalue != nullptr ? *pvalue : nullptr;
}

size_t grpc_chttp2_stream_map_size(grpc_chttp2_stream_map* map) {
  return map->count - map->free;
}

void* grpc_chttp2_stream_map_rand(grpc_chttp2_stream_map* map) {
  if (map->count == map->free) {
    return nullptr;
  }
  if (map->free != 0) {
    map->count = compact(map->keys, map->values, map->count);
    map->free = 0;
    GPR_ASSERT(map->count > 0);
  }
  return map->values[(static_cast<size_t>(rand())) % map->count];
}

void grpc_chttp2_stream_map_for_each(grpc_chttp2_stream_map* map,
                                     void (*f)(void* user_data, uint32_t key,
                                               void* value),
                                     void* user_data) {
  size_t i;

  for (i = 0; i < map->count; i++) {
    if (map->values[i]) {
      f(user_data, map->keys[i], map->values[i]);
    }
  }
}
