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

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <stdlib.h>
#include <string.h>
#include "src/core/lib/profiling/timers.h"

static void *zalloc_with_calloc(size_t sz) { return calloc(sz, 1); }

static void *zalloc_with_gpr_malloc(size_t sz) {
  void *p = gpr_malloc(sz);
  memset(p, 0, sz);
  return p;
}

static gpr_allocation_functions g_alloc_functions = {malloc, zalloc_with_calloc,
                                                     realloc, free};

gpr_allocation_functions gpr_get_allocation_functions() {
  return g_alloc_functions;
}

void gpr_set_allocation_functions(gpr_allocation_functions functions) {
  GPR_ASSERT(functions.malloc_fn != NULL);
  GPR_ASSERT(functions.realloc_fn != NULL);
  GPR_ASSERT(functions.free_fn != NULL);
  if (functions.zalloc_fn == NULL) {
    functions.zalloc_fn = zalloc_with_gpr_malloc;
  }
  g_alloc_functions = functions;
}

void *gpr_malloc(size_t size) {
  void *p;
  if (size == 0) return NULL;
  GPR_TIMER_BEGIN("gpr_malloc", 0);
  p = g_alloc_functions.malloc_fn(size);
  if (!p) {
    abort();
  }
  GPR_TIMER_END("gpr_malloc", 0);
  return p;
}

void *gpr_zalloc(size_t size) {
  void *p;
  if (size == 0) return NULL;
  GPR_TIMER_BEGIN("gpr_zalloc", 0);
  p = g_alloc_functions.zalloc_fn(size);
  if (!p) {
    abort();
  }
  GPR_TIMER_END("gpr_zalloc", 0);
  return p;
}

void gpr_free(void *p) {
  GPR_TIMER_BEGIN("gpr_free", 0);
  g_alloc_functions.free_fn(p);
  GPR_TIMER_END("gpr_free", 0);
}

void *gpr_realloc(void *p, size_t size) {
  if ((size == 0) && (p == NULL)) return NULL;
  GPR_TIMER_BEGIN("gpr_realloc", 0);
  p = g_alloc_functions.realloc_fn(p, size);
  if (!p) {
    abort();
  }
  GPR_TIMER_END("gpr_realloc", 0);
  return p;
}

void *gpr_malloc_aligned(size_t size, size_t alignment_log) {
  size_t alignment = ((size_t)1) << alignment_log;
  size_t extra = alignment - 1 + sizeof(void *);
  void *p = gpr_malloc(size + extra);
  void **ret = (void **)(((uintptr_t)p + extra) & ~(alignment - 1));
  ret[-1] = p;
  return (void *)ret;
}

void gpr_free_aligned(void *ptr) { gpr_free(((void **)ptr)[-1]); }
