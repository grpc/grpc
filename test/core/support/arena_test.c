/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/lib/support/arena.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include <inttypes.h>

#include "src/core/lib/support/string.h"
#include "test/core/util/test_config.h"

static void test_noop(void) { gpr_arena_destroy(gpr_arena_create(1)); }

static void test(const char *name, size_t init_size, const size_t *allocs,
                 size_t nallocs) {
  gpr_strvec v;
  char *s;
  gpr_strvec_init(&v);
  gpr_asprintf(&s, "test '%s': %" PRIdPTR " <- {", name, init_size);
  gpr_strvec_add(&v, s);
  for (size_t i = 0; i < nallocs; i++) {
    gpr_asprintf(&s, "%" PRIdPTR ",", allocs[i]);
    gpr_strvec_add(&v, s);
  }
  gpr_strvec_add(&v, gpr_strdup("}"));
  s = gpr_strvec_flatten(&v, NULL);
  gpr_strvec_destroy(&v);
  gpr_log(GPR_INFO, "%s", s);
  gpr_free(s);

  gpr_arena *a = gpr_arena_create(init_size);
  void **ps = gpr_zalloc(sizeof(*ps) * nallocs);
  for (size_t i = 0; i < nallocs; i++) {
    ps[i] = gpr_arena_alloc(a, allocs[i]);
    for (size_t j = 0; j < i; j++) {
      GPR_ASSERT(ps[i] != ps[j]);
    }
  }
  gpr_arena_destroy(a);
}

#define TEST(name, init_size, ...)                     \
  static const size_t allocs_##name[] = {__VA_ARGS__}; \
  test(#name, init_size, allocs_##name, GPR_ARRAY_SIZE(allocs_##name))

int main(int argc, char *argv[]) {
  grpc_test_init(argc, argv);

  test_noop();
  TEST(0_1, 0, 1);
  TEST(1_1, 1, 1);
  TEST(1_2, 1, 2);
  TEST(1_3, 1, 3);
  TEST(1_inc, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
  TEST(6_123, 6, 1, 2, 3);

  return 0;
}
