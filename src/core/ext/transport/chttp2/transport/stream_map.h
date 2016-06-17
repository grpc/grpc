/*
 *
 * Copyright 2015, Google Inc.
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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_MAP_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_MAP_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

/* Data structure to map a uint32_t to a data object (represented by a void*)

   Represented as a sorted array of keys, and a corresponding array of values.
   Lookups are performed with binary search.
   Adds are restricted to strictly higher keys than previously seen (this is
   guaranteed by http2). */
typedef struct {
  uint32_t *keys;
  void **values;
  size_t count;
  size_t free;
  size_t capacity;
} grpc_chttp2_stream_map;

void grpc_chttp2_stream_map_init(grpc_chttp2_stream_map *map,
                                 size_t initial_capacity);
void grpc_chttp2_stream_map_destroy(grpc_chttp2_stream_map *map);

/* Add a new key: given http2 semantics, new keys must always be greater than
   existing keys - this is asserted */
void grpc_chttp2_stream_map_add(grpc_chttp2_stream_map *map, uint32_t key,
                                void *value);

/* Delete an existing key - returns the previous value of the key if it existed,
   or NULL otherwise */
void *grpc_chttp2_stream_map_delete(grpc_chttp2_stream_map *map, uint32_t key);

/* Move all elements of src into dst */
void grpc_chttp2_stream_map_move_into(grpc_chttp2_stream_map *src,
                                      grpc_chttp2_stream_map *dst);

/* Return an existing key, or NULL if it does not exist */
void *grpc_chttp2_stream_map_find(grpc_chttp2_stream_map *map, uint32_t key);

/* How many (populated) entries are in the stream map? */
size_t grpc_chttp2_stream_map_size(grpc_chttp2_stream_map *map);

/* Callback on each stream */
void grpc_chttp2_stream_map_for_each(grpc_chttp2_stream_map *map,
                                     void (*f)(void *user_data, uint32_t key,
                                               void *value),
                                     void *user_data);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_MAP_H */
