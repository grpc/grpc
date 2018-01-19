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

#include <grpc/support/alloc.h>

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <stdlib.h>
#include <string.h>
#include "src/core/lib/profiling/timers.h"

static void* zalloc_with_calloc(size_t sz) { return calloc(sz, 1); }

static void* zalloc_with_gpr_malloc(size_t sz) {
  void* p = gpr_malloc(sz);
  memset(p, 0, sz);
  return p;
}

static gpr_allocation_functions g_alloc_functions = {malloc, zalloc_with_calloc,
                                                     realloc, free};

gpr_allocation_functions gpr_get_allocation_functions() {
  return g_alloc_functions;
}

void gpr_set_allocation_functions(gpr_allocation_functions functions) {
  GPR_ASSERT(functions.malloc_fn != nullptr);
  GPR_ASSERT(functions.realloc_fn != nullptr);
  GPR_ASSERT(functions.free_fn != nullptr);
  if (functions.zalloc_fn == nullptr) {
    functions.zalloc_fn = zalloc_with_gpr_malloc;
  }
  g_alloc_functions = functions;
}

void* gpr_malloc(size_t size) {
  void* p;
  if (size == 0) return nullptr;
  GPR_TIMER_BEGIN("gpr_malloc", 0);
  p = g_alloc_functions.malloc_fn(size);
  if (!p) {
    abort();
  }
  GPR_TIMER_END("gpr_malloc", 0);
  return p;
}

void* gpr_zalloc(size_t size) {
  void* p;
  if (size == 0) return nullptr;
  GPR_TIMER_BEGIN("gpr_zalloc", 0);
  p = g_alloc_functions.zalloc_fn(size);
  if (!p) {
    abort();
  }
  GPR_TIMER_END("gpr_zalloc", 0);
  return p;
}

void gpr_free(void* p) {
  GPR_TIMER_BEGIN("gpr_free", 0);
  g_alloc_functions.free_fn(p);
  GPR_TIMER_END("gpr_free", 0);
}

void* gpr_realloc(void* p, size_t size) {
  if ((size == 0) && (p == nullptr)) return nullptr;
  GPR_TIMER_BEGIN("gpr_realloc", 0);
  p = g_alloc_functions.realloc_fn(p, size);
  if (!p) {
    abort();
  }
  GPR_TIMER_END("gpr_realloc", 0);
  return p;
}

void* gpr_malloc_aligned(size_t size, size_t alignment_log) {
  size_t alignment = ((size_t)1) << alignment_log;
  size_t extra = alignment - 1 + sizeof(void*);
  void* p = gpr_malloc(size + extra);
  void** ret = (void**)(((uintptr_t)p + extra) & ~(alignment - 1));
  ret[-1] = p;
  return (void*)ret;
}

void gpr_free_aligned(void* ptr) { gpr_free(((void**)ptr)[-1]); }
