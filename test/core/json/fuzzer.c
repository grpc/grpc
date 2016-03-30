/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include <stdint.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/json/json.h"

static size_t g_total_size = 0;
static gpr_allocation_functions g_old_allocs;

void *guard_malloc(size_t size) {
  size_t *ptr;
  g_total_size += size;
  ptr = g_old_allocs.malloc_fn(size + sizeof(size));
  *ptr++ = size;
  return ptr;
}

void *guard_realloc(void *vptr, size_t size) {
  size_t *ptr = vptr;
  --ptr;
  g_total_size -= *ptr;
  ptr = g_old_allocs.realloc_fn(ptr, size + sizeof(size));
  g_total_size += size;
  *ptr++ = size;
  return ptr;
}

void guard_free(void *vptr) {
  size_t *ptr = vptr;
  --ptr;
  g_total_size -= *ptr;
  g_old_allocs.free_fn(ptr);
}

struct gpr_allocation_functions g_guard_allocs = {
  guard_malloc,
  guard_realloc,
  guard_free
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char *s;
  g_old_allocs = gpr_get_allocation_functions();
  gpr_set_allocation_functions(g_guard_allocs);
  s = gpr_malloc(size);
  memcpy(s, data, size);
  grpc_json *x;
  if ((x = grpc_json_parse_string_with_len(s, size))) {
    grpc_json_destroy(x);
  }
  gpr_free(s);
  gpr_set_allocation_functions(g_old_allocs);
  GPR_ASSERT(g_total_size == 0);
  return 0;
}
