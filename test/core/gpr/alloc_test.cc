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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

static void* fake_malloc(size_t size) { return (void*)size; }

static void* fake_realloc(void* addr, size_t size) { return (void*)size; }

static void fake_free(void* addr) {
  *(static_cast<intptr_t*>(addr)) = static_cast<intptr_t>(0xdeadd00d);
}

static void test_custom_allocs() {
  const gpr_allocation_functions default_fns = gpr_get_allocation_functions();
  intptr_t addr_to_free = 0;
  char* i;
  gpr_allocation_functions fns = {fake_malloc, nullptr, fake_realloc,
                                  fake_free};

  gpr_set_allocation_functions(fns);
  GPR_ASSERT((void*)(size_t)0xdeadbeef == gpr_malloc(0xdeadbeef));
  GPR_ASSERT((void*)(size_t)0xcafed00d == gpr_realloc(nullptr, 0xcafed00d));

  gpr_free(&addr_to_free);
  GPR_ASSERT(addr_to_free == (intptr_t)0xdeadd00d);

  /* Restore and check we don't get funky values and that we don't leak */
  gpr_set_allocation_functions(default_fns);
  GPR_ASSERT((void*)sizeof(*i) !=
             (i = static_cast<char*>(gpr_malloc(sizeof(*i)))));
  GPR_ASSERT((void*)2 != (i = static_cast<char*>(gpr_realloc(i, 2))));
  gpr_free(i);
}

static void test_malloc_aligned() {
  for (size_t size = 1; size <= 256; ++size) {
    void* ptr = gpr_malloc_aligned(size, 16);
    GPR_ASSERT(ptr != nullptr);
    GPR_ASSERT(((intptr_t)ptr & 0xf) == 0);
    memset(ptr, 0, size);
    gpr_free_aligned(ptr);
  }
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  test_custom_allocs();
  test_malloc_aligned();
  return 0;
}
