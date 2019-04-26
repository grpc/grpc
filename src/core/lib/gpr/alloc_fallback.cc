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

#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/alloc.h"

void* gpr_malloc_aligned_fallback(size_t size, size_t alignment) {
  GPR_DEBUG_ASSERT(is_power_of_two(alignment));
  size_t extra = alignment - 1 + sizeof(void*);
  void* p = gpr_malloc(size + extra);
  void** ret = (void**)(((uintptr_t)p + extra) & ~(alignment - 1));
  ret[-1] = p;
  return (void*)ret;
}

void gpr_free_aligned_fallback(void* ptr) {
  gpr_free((static_cast<void**>(ptr))[-1]);
}

#if defined(GRPC_NEED_FALLBACK_ALIGNED_MALLOC)

void* gpr_malloc_aligned_platform(size_t size, size_t alignment) {
  return gpr_malloc_aligned_fallback(size, alignment);
}

void gpr_free_aligned_platform(void* ptr) { gpr_free_aligned_fallback(ptr); }

#endif /* GRPC_NEED_FALLBACK_ALIGNED_MALLOC */
