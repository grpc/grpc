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

#ifndef GRPC_C_INTERNAL_ARRAY_H
#define GRPC_C_INTERNAL_ARRAY_H

#include <stddef.h>
#include <stdlib.h>

/**
 * Implements a generic array structure
 * Usage:
 * GRPC_array(int) arr;
 * GRPC_array_init(arr);
 * GRPC_array_push_back(arr, 5);
 * // arr.data[0] == 5;
 * GRPC_array_deinit(arr);
 */

typedef struct GRPC_array_state {
  size_t size;
  size_t capacity;
} GRPC_array_state;

void GRPC_array_init_impl(GRPC_array_state *state, void *data_ptr,
                          size_t elem_size);
void GRPC_array_pop_back_impl(GRPC_array_state *state, void *data_ptr,
                              size_t elem_size);
void GRPC_array_deinit_impl(GRPC_array_state *state, void *data_ptr,
                            size_t elem_size);
void GRPC_array_ensure_capacity(GRPC_array_state *state, void *data_ptr,
                                size_t elem_size, size_t target_size);

#define GRPC_array(type)    \
  struct {                  \
    type *data;             \
    GRPC_array_state state; \
  }
#define GRPC_array_init(arr) \
  GRPC_array_init_impl(&arr.state, &arr.data, sizeof(*arr.data));
// Cannot delegate to a function since we do not know the type of input
#define GRPC_array_push_back(arr, ...)                                   \
  {                                                                      \
    GRPC_array_ensure_capacity(&arr.state, &arr.data, sizeof(*arr.data), \
                               arr.state.size + 1);                      \
    arr.data[arr.state.size++] = (__VA_ARGS__);                          \
  }
#define GRPC_array_pop_back(arr) \
  GRPC_array_pop_back_impl(&arr.state, &arr.data, sizeof(*arr.data));
#define GRPC_array_deinit(arr) \
  GRPC_array_deinit_impl(&arr.state, &arr.data, sizeof(*arr.data));

#endif  // GRPC_C_INTERNAL_ARRAY_H
