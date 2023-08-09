//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"

void* gpr_malloc(size_t size) {
  void* p;
  if (size == 0) return nullptr;
  p = malloc(size);
  if (!p) {
    abort();
  }
  return p;
}

void* gpr_zalloc(size_t size) {
  void* p;
  if (size == 0) return nullptr;
  p = calloc(size, 1);
  if (!p) {
    abort();
  }
  return p;
}

void gpr_free(void* p) { free(p); }

void* gpr_realloc(void* p, size_t size) {
  if ((size == 0) && (p == nullptr)) return nullptr;
  p = realloc(p, size);
  if (!p) {
    abort();
  }
  return p;
}

void* gpr_malloc_aligned(size_t size, size_t alignment) {
  GPR_ASSERT(((alignment - 1) & alignment) == 0);  // Must be power of 2.
  size_t extra = alignment - 1 + sizeof(void*);
  void* p = gpr_malloc(size + extra);
  void** ret = reinterpret_cast<void**>(
      (reinterpret_cast<uintptr_t>(p) + extra) & ~(alignment - 1));
  ret[-1] = p;
  return ret;
}

void gpr_free_aligned(void* ptr) { gpr_free((static_cast<void**>(ptr))[-1]); }
