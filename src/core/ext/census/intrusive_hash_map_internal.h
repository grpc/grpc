/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_EXT_CENSUS_INTRUSIVE_HASH_MAP_INTERNAL_H
#define GRPC_CORE_EXT_CENSUS_INTRUSIVE_HASH_MAP_INTERNAL_H

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>
#include <stdbool.h>

/* The chunked vector is a data structure that allocates buckets for use in the
 * hash map. ChunkedVector is logically equivalent to T*[N] (cast void* as
 * T*). It's internally implemented as an array of 1MB arrays to avoid
 * allocating large consecutive memory chunks. This is an internal data
 * structure that should never be accessed directly. */
typedef struct chunked_vector {
  size_t size_;
  void **first_;
  void ***rest_;
} chunked_vector;

/* Core intrusive hash map data structure. All internal elements are managed by
 * functions and should not be altered manually. */
typedef struct intrusive_hash_map {
  uint32_t num_items;
  uint32_t extend_threshold;
  uint32_t log2_num_buckets;
  uint32_t hash_mask;
  chunked_vector buckets;
} intrusive_hash_map;

#endif /* GRPC_CORE_EXT_CENSUS_INTRUSIVE_HASH_MAP_INTERNAL_H */
