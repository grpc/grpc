/*
 * Copyright (c) 2009-2022, Google LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Google LLC nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL Google LLC BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "upb/io/chunked_input_stream.h"

// Must be last.
#include "upb/port/def.inc"

typedef struct {
  upb_ZeroCopyInputStream base;

  const char* data;
  size_t size;
  size_t limit;
  size_t position;
  size_t last_returned_size;
} upb_ChunkedInputStream;

static const void* upb_ChunkedInputStream_Next(upb_ZeroCopyInputStream* z,
                                               size_t* count,
                                               upb_Status* status) {
  upb_ChunkedInputStream* c = (upb_ChunkedInputStream*)z;
  UPB_ASSERT(c->position <= c->size);

  const char* out = c->data + c->position;

  const size_t chunk = UPB_MIN(c->limit, c->size - c->position);
  c->position += chunk;
  c->last_returned_size = chunk;
  *count = chunk;

  return chunk ? out : NULL;
}

static void upb_ChunkedInputStream_BackUp(upb_ZeroCopyInputStream* z,
                                          size_t count) {
  upb_ChunkedInputStream* c = (upb_ChunkedInputStream*)z;

  UPB_ASSERT(c->last_returned_size >= count);
  c->position -= count;
  c->last_returned_size -= count;
}

static bool upb_ChunkedInputStream_Skip(upb_ZeroCopyInputStream* z,
                                        size_t count) {
  upb_ChunkedInputStream* c = (upb_ChunkedInputStream*)z;

  c->last_returned_size = 0;  // Don't let caller back up.
  if (count > c->size - c->position) {
    c->position = c->size;
    return false;
  }

  c->position += count;
  return true;
}

static size_t upb_ChunkedInputStream_ByteCount(
    const upb_ZeroCopyInputStream* z) {
  const upb_ChunkedInputStream* c = (const upb_ChunkedInputStream*)z;

  return c->position;
}

static const _upb_ZeroCopyInputStream_VTable upb_ChunkedInputStream_vtable = {
    upb_ChunkedInputStream_Next,
    upb_ChunkedInputStream_BackUp,
    upb_ChunkedInputStream_Skip,
    upb_ChunkedInputStream_ByteCount,
};

upb_ZeroCopyInputStream* upb_ChunkedInputStream_New(const void* data,
                                                    size_t size, size_t limit,
                                                    upb_Arena* arena) {
  upb_ChunkedInputStream* c = upb_Arena_Malloc(arena, sizeof(*c));
  if (!c || !limit) return NULL;

  c->base.vtable = &upb_ChunkedInputStream_vtable;
  c->data = data;
  c->size = size;
  c->limit = limit;
  c->position = 0;
  c->last_returned_size = 0;

  return (upb_ZeroCopyInputStream*)c;
}
