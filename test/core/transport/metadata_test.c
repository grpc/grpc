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

#include "src/core/transport/metadata.h"

#include <stdio.h>

#include "src/core/support/string.h"
#include "src/core/transport/chttp2/bin_encoder.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "test/core/util/test_config.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x)

/* a large number */
#define MANY 10000

static void test_no_op(void) {
  grpc_mdctx *ctx;

  LOG_TEST("test_no_op");

  ctx = grpc_mdctx_create();
  grpc_mdctx_unref(ctx);
}

static void test_create_string(void) {
  grpc_mdctx *ctx;
  grpc_mdstr *s1, *s2, *s3;

  LOG_TEST("test_create_string");

  ctx = grpc_mdctx_create();
  s1 = grpc_mdstr_from_string(ctx, "hello");
  s2 = grpc_mdstr_from_string(ctx, "hello");
  s3 = grpc_mdstr_from_string(ctx, "very much not hello");
  GPR_ASSERT(s1 == s2);
  GPR_ASSERT(s3 != s1);
  GPR_ASSERT(gpr_slice_str_cmp(s1->slice, "hello") == 0);
  GPR_ASSERT(gpr_slice_str_cmp(s3->slice, "very much not hello") == 0);
  GRPC_MDSTR_UNREF(s1);
  GRPC_MDSTR_UNREF(s2);
  grpc_mdctx_unref(ctx);
  GRPC_MDSTR_UNREF(s3);
}

static void test_create_metadata(void) {
  grpc_mdctx *ctx;
  grpc_mdelem *m1, *m2, *m3;

  LOG_TEST("test_create_metadata");

  ctx = grpc_mdctx_create();
  m1 = grpc_mdelem_from_strings(ctx, "a", "b");
  m2 = grpc_mdelem_from_strings(ctx, "a", "b");
  m3 = grpc_mdelem_from_strings(ctx, "a", "c");
  GPR_ASSERT(m1 == m2);
  GPR_ASSERT(m3 != m1);
  GPR_ASSERT(m3->key == m1->key);
  GPR_ASSERT(m3->value != m1->value);
  GPR_ASSERT(gpr_slice_str_cmp(m1->key->slice, "a") == 0);
  GPR_ASSERT(gpr_slice_str_cmp(m1->value->slice, "b") == 0);
  GPR_ASSERT(gpr_slice_str_cmp(m3->value->slice, "c") == 0);
  GRPC_MDELEM_UNREF(m1);
  GRPC_MDELEM_UNREF(m2);
  GRPC_MDELEM_UNREF(m3);
  grpc_mdctx_unref(ctx);
}

static void test_create_many_ephemeral_metadata(void) {
  grpc_mdctx *ctx;
  char buffer[GPR_LTOA_MIN_BUFSIZE];
  long i;
  size_t mdtab_capacity_before;

  LOG_TEST("test_create_many_ephemeral_metadata");

  ctx = grpc_mdctx_create();
  mdtab_capacity_before = grpc_mdctx_get_mdtab_capacity_test_only(ctx);
  /* add, and immediately delete a bunch of different elements */
  for (i = 0; i < MANY; i++) {
    gpr_ltoa(i, buffer);
    GRPC_MDELEM_UNREF(grpc_mdelem_from_strings(ctx, "a", buffer));
  }
  /* capacity should not grow */
  GPR_ASSERT(mdtab_capacity_before ==
             grpc_mdctx_get_mdtab_capacity_test_only(ctx));
  grpc_mdctx_unref(ctx);
}

static void test_create_many_persistant_metadata(void) {
  grpc_mdctx *ctx;
  char buffer[GPR_LTOA_MIN_BUFSIZE];
  long i;
  grpc_mdelem **created = gpr_malloc(sizeof(grpc_mdelem *) * MANY);
  grpc_mdelem *md;

  LOG_TEST("test_create_many_persistant_metadata");

  ctx = grpc_mdctx_create();
  /* add phase */
  for (i = 0; i < MANY; i++) {
    gpr_ltoa(i, buffer);
    created[i] = grpc_mdelem_from_strings(ctx, "a", buffer);
  }
  /* verify phase */
  for (i = 0; i < MANY; i++) {
    gpr_ltoa(i, buffer);
    md = grpc_mdelem_from_strings(ctx, "a", buffer);
    GPR_ASSERT(md == created[i]);
    GRPC_MDELEM_UNREF(md);
  }
  /* cleanup phase */
  for (i = 0; i < MANY; i++) {
    GRPC_MDELEM_UNREF(created[i]);
  }
  grpc_mdctx_unref(ctx);

  gpr_free(created);
}

static void test_spin_creating_the_same_thing(void) {
  grpc_mdctx *ctx;

  LOG_TEST("test_spin_creating_the_same_thing");

  ctx = grpc_mdctx_create();
  GPR_ASSERT(grpc_mdctx_get_mdtab_count_test_only(ctx) == 0);
  GPR_ASSERT(grpc_mdctx_get_mdtab_free_test_only(ctx) == 0);

  GRPC_MDELEM_UNREF(grpc_mdelem_from_strings(ctx, "a", "b"));
  GPR_ASSERT(grpc_mdctx_get_mdtab_count_test_only(ctx) == 1);
  GPR_ASSERT(grpc_mdctx_get_mdtab_free_test_only(ctx) == 1);

  GRPC_MDELEM_UNREF(grpc_mdelem_from_strings(ctx, "a", "b"));
  GPR_ASSERT(grpc_mdctx_get_mdtab_count_test_only(ctx) == 1);
  GPR_ASSERT(grpc_mdctx_get_mdtab_free_test_only(ctx) == 1);

  GRPC_MDELEM_UNREF(grpc_mdelem_from_strings(ctx, "a", "b"));
  GPR_ASSERT(grpc_mdctx_get_mdtab_count_test_only(ctx) == 1);
  GPR_ASSERT(grpc_mdctx_get_mdtab_free_test_only(ctx) == 1);

  grpc_mdctx_unref(ctx);
}

static void test_things_stick_around(void) {
  grpc_mdctx *ctx;
  size_t i, j;
  char *buffer;
  size_t nstrs = 1000;
  grpc_mdstr **strs = gpr_malloc(sizeof(grpc_mdstr *) * nstrs);
  size_t *shuf = gpr_malloc(sizeof(size_t) * nstrs);
  grpc_mdstr *test;

  LOG_TEST("test_things_stick_around");

  ctx = grpc_mdctx_create();

  for (i = 0; i < nstrs; i++) {
    gpr_asprintf(&buffer, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx%dx", i);
    strs[i] = grpc_mdstr_from_string(ctx, buffer);
    shuf[i] = i;
    gpr_free(buffer);
  }

  for (i = 0; i < nstrs; i++) {
    GRPC_MDSTR_REF(strs[i]);
    GRPC_MDSTR_UNREF(strs[i]);
  }

  for (i = 0; i < nstrs; i++) {
    size_t p = (size_t)rand() % nstrs;
    size_t q = (size_t)rand() % nstrs;
    size_t temp = shuf[p];
    shuf[p] = shuf[q];
    shuf[q] = temp;
  }

  for (i = 0; i < nstrs; i++) {
    GRPC_MDSTR_UNREF(strs[shuf[i]]);
    for (j = i + 1; j < nstrs; j++) {
      gpr_asprintf(&buffer, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx%dx", shuf[j]);
      test = grpc_mdstr_from_string(ctx, buffer);
      GPR_ASSERT(test == strs[shuf[j]]);
      GRPC_MDSTR_UNREF(test);
      gpr_free(buffer);
    }
  }

  grpc_mdctx_unref(ctx);
  gpr_free(strs);
  gpr_free(shuf);
}

static void test_slices_work(void) {
  /* ensure no memory leaks when switching representation from mdstr to slice */
  grpc_mdctx *ctx;
  grpc_mdstr *str;
  gpr_slice slice;

  LOG_TEST("test_slices_work");

  ctx = grpc_mdctx_create();

  str = grpc_mdstr_from_string(
      ctx, "123456789012345678901234567890123456789012345678901234567890");
  slice = gpr_slice_ref(str->slice);
  GRPC_MDSTR_UNREF(str);
  gpr_slice_unref(slice);

  str = grpc_mdstr_from_string(
      ctx, "123456789012345678901234567890123456789012345678901234567890");
  slice = gpr_slice_ref(str->slice);
  gpr_slice_unref(slice);
  GRPC_MDSTR_UNREF(str);

  grpc_mdctx_unref(ctx);
}

static void test_base64_and_huffman_works(void) {
  grpc_mdctx *ctx;
  grpc_mdstr *str;
  gpr_slice slice1;
  gpr_slice slice2;

  LOG_TEST("test_base64_and_huffman_works");

  ctx = grpc_mdctx_create();
  str = grpc_mdstr_from_string(ctx, "abcdefg");
  slice1 = grpc_mdstr_as_base64_encoded_and_huffman_compressed(str);
  slice2 = grpc_chttp2_base64_encode_and_huffman_compress(str->slice);
  GPR_ASSERT(0 == gpr_slice_cmp(slice1, slice2));

  gpr_slice_unref(slice2);
  GRPC_MDSTR_UNREF(str);
  grpc_mdctx_unref(ctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_no_op();
  test_create_string();
  test_create_metadata();
  test_create_many_ephemeral_metadata();
  test_create_many_persistant_metadata();
  test_spin_creating_the_same_thing();
  test_things_stick_around();
  test_slices_work();
  test_base64_and_huffman_works();
  return 0;
}
