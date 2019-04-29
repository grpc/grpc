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

#include <grpc/support/alloc.h>

#include <grpc/support/log.h>
#include <stdlib.h>
#include <string.h>
#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/profiling/timers.h"

static void* zalloc_with_calloc(size_t sz) { return calloc(sz, 1); }

static void* zalloc_with_gpr_malloc(size_t sz) {
  void* p = gpr_malloc(sz);
  memset(p, 0, sz);
  return p;
}

#ifndef NDEBUG
static constexpr bool is_power_of_two(size_t value) {
  // 2^N =     100000...000
  // 2^N - 1 = 011111...111
  // (2^N) && ((2^N)-1)) = 0
  return (value & (value - 1)) == 0;
}
#endif

static void* aligned_alloc_with_gpr_malloc(size_t size, size_t alignment) {
  GPR_DEBUG_ASSERT(is_power_of_two(alignment));
  size_t extra = alignment - 1 + sizeof(void*);
  void* p = gpr_malloc(size + extra);
  void** ret = (void**)(((uintptr_t)p + extra) & ~(alignment - 1));
  ret[-1] = p;
  return (void*)ret;
}

static void aligned_free_with_gpr_malloc(void* ptr) {
  gpr_free((static_cast<void**>(ptr))[-1]);
}

static void* platform_malloc_aligned(size_t size, size_t alignment) {
#if defined(GPR_HAS_ALIGNED_ALLOC)
  size = GPR_ROUND_UP_TO_ALIGNMENT_SIZE(size, alignment);
  void* ret = aligned_alloc(alignment, size);
  GPR_ASSERT(ret != nullptr);
  return ret;
#elif defined(GPR_HAS_ALIGNED_MALLOC)
  GPR_DEBUG_ASSERT(is_power_of_two(alignment));
  void* ret = _aligned_malloc(size, alignment);
  GPR_ASSERT(ret != nullptr);
  return ret;
#elif defined(GPR_HAS_POSIX_MEMALIGN)
  GPR_DEBUG_ASSERT(is_power_of_two(alignment));
  GPR_DEBUG_ASSERT(alignment % sizeof(void*) == 0);
  void* ret = nullptr;
  GPR_ASSERT(posix_memalign(&ret, alignment, size) == 0);
  return ret;
#else
  return aligned_alloc_with_gpr_malloc(size, alignment);
#endif
}

static void platform_free_aligned(void* ptr) {
#if defined(GPR_HAS_ALIGNED_ALLOC) || defined(GPR_HAS_POSIX_MEMALIGN)
  free(ptr);
#elif defined(GPR_HAS_ALIGNED_MALLOC)
  _aligned_free(ptr);
#else
  aligned_free_with_gpr_malloc(ptr);
#endif
}

static gpr_allocation_functions g_alloc_functions = {
    malloc, zalloc_with_calloc,      realloc,
    free,   platform_malloc_aligned, platform_free_aligned};

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
  GPR_ASSERT((functions.aligned_alloc_fn == nullptr) ==
             (functions.aligned_free_fn == nullptr));
  if (functions.aligned_alloc_fn == nullptr) {
    functions.aligned_alloc_fn = aligned_alloc_with_gpr_malloc;
    functions.aligned_free_fn = aligned_free_with_gpr_malloc;
  }
  g_alloc_functions = functions;
}

void* gpr_malloc(size_t size) {
  GPR_TIMER_SCOPE("gpr_malloc", 0);
  void* p;
  if (size == 0) return nullptr;
  p = g_alloc_functions.malloc_fn(size);
  if (!p) {
    abort();
  }
  return p;
}

void* gpr_zalloc(size_t size) {
  GPR_TIMER_SCOPE("gpr_zalloc", 0);
  void* p;
  if (size == 0) return nullptr;
  p = g_alloc_functions.zalloc_fn(size);
  if (!p) {
    abort();
  }
  return p;
}

void gpr_free(void* p) {
  GPR_TIMER_SCOPE("gpr_free", 0);
  g_alloc_functions.free_fn(p);
}

void* gpr_realloc(void* p, size_t size) {
  GPR_TIMER_SCOPE("gpr_realloc", 0);
  if ((size == 0) && (p == nullptr)) return nullptr;
  p = g_alloc_functions.realloc_fn(p, size);
  if (!p) {
    abort();
  }
  return p;
}

void* gpr_malloc_aligned(size_t size, size_t alignment) {
  GPR_TIMER_SCOPE("gpr_malloc_aligned", 0);
  if (size == 0) return nullptr;
  return g_alloc_functions.aligned_alloc_fn(size, alignment);
}

void gpr_free_aligned(void* ptr) {
  GPR_TIMER_SCOPE("gpr_free_aligned", 0);
  g_alloc_functions.aligned_free_fn(ptr);
}
