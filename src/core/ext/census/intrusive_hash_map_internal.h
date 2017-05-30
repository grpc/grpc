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
