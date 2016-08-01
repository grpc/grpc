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

#include <grpc/support/alloc.h>
#include <grpc_c/codegen/pb_compat.h>
#include <stddef.h>
#include <third_party/nanopb/pb.h>
#include <third_party/nanopb/pb_decode.h>
#include <third_party/nanopb/pb_encode.h>
#include "src/c/alloc.h"

/**
 * This file implements a Nanopb stream used to collect deserialized data from
 * Nanopb.
 */

typedef struct GRPC_pb_dynamic_array_state {
  void *data;
  size_t size;
  size_t capacity;
} GRPC_pb_dynamic_array_state;

static size_t upper_power_of_two(size_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
}

GRPC_pb_dynamic_array_state *GRPC_pb_compat_dynamic_array_alloc() {
  return GRPC_ALLOC_STRUCT(GRPC_pb_dynamic_array_state,
                           {.data = NULL, .size = 0, .capacity = 0});
}

void GRPC_pb_compat_dynamic_array_free(GRPC_pb_dynamic_array_state *state) {
  gpr_free(state);
}

bool GRPC_pb_compat_dynamic_array_callback(pb_ostream_t *stream,
                                           const uint8_t *buf, size_t count) {
  GRPC_pb_dynamic_array_state *state = stream->state;
  if (state->size + count > state->capacity) {
    state->capacity = upper_power_of_two(state->size + count);
    state->data = gpr_realloc(state->data, state->capacity);
  }
  if (state->data == NULL) return false;
  if (buf) memcpy((char *)state->data + state->size, buf, count);
  state->size += count;
  return true;
}

void *GRPC_pb_compat_dynamic_array_get_content(
    GRPC_pb_dynamic_array_state *state) {
  return state->data;
}

GRPC_message GRPC_pb_compat_generic_serializer(const GRPC_message input,
                                               const void *fields) {
  pb_ostream_t ostream = {.callback = GRPC_pb_compat_dynamic_array_callback,
                          .state = GRPC_pb_compat_dynamic_array_alloc(),
                          .max_size = SIZE_MAX};
  pb_encode(&ostream, fields, input.data);
  GRPC_message msg =
      (GRPC_message){GRPC_pb_compat_dynamic_array_get_content(ostream.state),
                     ostream.bytes_written};
  GRPC_pb_compat_dynamic_array_free(ostream.state);
  return msg;
}

void GRPC_pb_compat_generic_deserializer(const GRPC_message input, void *output,
                                         const void *fields) {
  pb_istream_t istream =
      pb_istream_from_buffer((void *)input.data, input.length);
  pb_decode(&istream, fields, output);
}
