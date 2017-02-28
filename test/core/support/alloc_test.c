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
#include "test/core/util/test_config.h"

static void *fake_malloc(size_t size) { return (void *)size; }

static void *fake_realloc(void *addr, size_t size) { return (void *)size; }

static void fake_free(void *addr) {
  *((intptr_t *)addr) = (intptr_t)0xdeadd00d;
}

static void test_custom_allocs() {
  const gpr_allocation_functions default_fns = gpr_get_allocation_functions();
  intptr_t addr_to_free = 0;
  char *i;
  gpr_allocation_functions fns = {fake_malloc, NULL, fake_realloc, fake_free};

  gpr_set_allocation_functions(fns);
  GPR_ASSERT((void *)(size_t)0xdeadbeef == gpr_malloc(0xdeadbeef));
  GPR_ASSERT((void *)(size_t)0xcafed00d == gpr_realloc(0, 0xcafed00d));

  gpr_free(&addr_to_free);
  GPR_ASSERT(addr_to_free == (intptr_t)0xdeadd00d);

  /* Restore and check we don't get funky values and that we don't leak */
  gpr_set_allocation_functions(default_fns);
  GPR_ASSERT((void *)sizeof(*i) != (i = gpr_malloc(sizeof(*i))));
  GPR_ASSERT((void *)2 != (i = gpr_realloc(i, 2)));
  gpr_free(i);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_custom_allocs();
  return 0;
}
