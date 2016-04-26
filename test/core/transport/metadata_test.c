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

#include "src/core/lib/transport/metadata.h"

#include <stdio.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/transport/static_metadata.h"
#include "test/core/util/test_config.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x)

/* a large number */
#define MANY 10000

static void test_no_op(void) {
  LOG_TEST("test_no_op");
  grpc_init();
  grpc_shutdown();
}

static void test_create_string(void) {
  grpc_mdstr *s1, *s2, *s3;

  LOG_TEST("test_create_string");

  grpc_init();
  s1 = grpc_mdstr_from_string("hello");
  s2 = grpc_mdstr_from_string("hello");
  s3 = grpc_mdstr_from_string("very much not hello");
  GPR_ASSERT(s1 == s2);
  GPR_ASSERT(s3 != s1);
  GPR_ASSERT(gpr_slice_str_cmp(s1->slice, "hello") == 0);
  GPR_ASSERT(gpr_slice_str_cmp(s3->slice, "very much not hello") == 0);
  GRPC_MDSTR_UNREF(s1);
  GRPC_MDSTR_UNREF(s2);
  GRPC_MDSTR_UNREF(s3);
  grpc_shutdown();
}

static void test_create_metadata(void) {
  grpc_mdelem *m1, *m2, *m3;

  LOG_TEST("test_create_metadata");

  grpc_init();
  m1 = grpc_mdelem_from_strings("a", "b");
  m2 = grpc_mdelem_from_strings("a", "b");
  m3 = grpc_mdelem_from_strings("a", "c");
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
  grpc_shutdown();
}

static void test_create_many_ephemeral_metadata(void) {
  char buffer[GPR_LTOA_MIN_BUFSIZE];
  long i;

  LOG_TEST("test_create_many_ephemeral_metadata");

  grpc_init();
  /* add, and immediately delete a bunch of different elements */
  for (i = 0; i < MANY; i++) {
    gpr_ltoa(i, buffer);
    GRPC_MDELEM_UNREF(grpc_mdelem_from_strings("a", buffer));
  }
  grpc_shutdown();
}

static void test_create_many_persistant_metadata(void) {
  char buffer[GPR_LTOA_MIN_BUFSIZE];
  long i;
  grpc_mdelem **created = gpr_malloc(sizeof(grpc_mdelem *) * MANY);
  grpc_mdelem *md;

  LOG_TEST("test_create_many_persistant_metadata");

  grpc_init();
  /* add phase */
  for (i = 0; i < MANY; i++) {
    gpr_ltoa(i, buffer);
    created[i] = grpc_mdelem_from_strings("a", buffer);
  }
  /* verify phase */
  for (i = 0; i < MANY; i++) {
    gpr_ltoa(i, buffer);
    md = grpc_mdelem_from_strings("a", buffer);
    GPR_ASSERT(md == created[i]);
    GRPC_MDELEM_UNREF(md);
  }
  /* cleanup phase */
  for (i = 0; i < MANY; i++) {
    GRPC_MDELEM_UNREF(created[i]);
  }
  grpc_shutdown();

  gpr_free(created);
}

static void test_spin_creating_the_same_thing(void) {
  LOG_TEST("test_spin_creating_the_same_thing");

  grpc_init();
  GRPC_MDELEM_UNREF(grpc_mdelem_from_strings("a", "b"));
  GRPC_MDELEM_UNREF(grpc_mdelem_from_strings("a", "b"));
  GRPC_MDELEM_UNREF(grpc_mdelem_from_strings("a", "b"));
  grpc_shutdown();
}

static void test_things_stick_around(void) {
  size_t i, j;
  char *buffer;
  size_t nstrs = 1000;
  grpc_mdstr **strs = gpr_malloc(sizeof(grpc_mdstr *) * nstrs);
  size_t *shuf = gpr_malloc(sizeof(size_t) * nstrs);
  grpc_mdstr *test;

  LOG_TEST("test_things_stick_around");

  grpc_init();

  for (i = 0; i < nstrs; i++) {
    gpr_asprintf(&buffer, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx%dx", i);
    strs[i] = grpc_mdstr_from_string(buffer);
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
      test = grpc_mdstr_from_string(buffer);
      GPR_ASSERT(test == strs[shuf[j]]);
      GRPC_MDSTR_UNREF(test);
      gpr_free(buffer);
    }
  }

  grpc_shutdown();
  gpr_free(strs);
  gpr_free(shuf);
}

static void test_slices_work(void) {
  /* ensure no memory leaks when switching representation from mdstr to slice */
  grpc_mdstr *str;
  gpr_slice slice;

  LOG_TEST("test_slices_work");

  grpc_init();

  str = grpc_mdstr_from_string(
      "123456789012345678901234567890123456789012345678901234567890");
  slice = gpr_slice_ref(str->slice);
  GRPC_MDSTR_UNREF(str);
  gpr_slice_unref(slice);

  str = grpc_mdstr_from_string(
      "123456789012345678901234567890123456789012345678901234567890");
  slice = gpr_slice_ref(str->slice);
  gpr_slice_unref(slice);
  GRPC_MDSTR_UNREF(str);

  grpc_shutdown();
}

static void test_base64_and_huffman_works(void) {
  grpc_mdstr *str;
  gpr_slice slice1;
  gpr_slice slice2;

  LOG_TEST("test_base64_and_huffman_works");

  grpc_init();
  str = grpc_mdstr_from_string("abcdefg");
  slice1 = grpc_mdstr_as_base64_encoded_and_huffman_compressed(str);
  slice2 = grpc_chttp2_base64_encode_and_huffman_compress(str->slice);
  GPR_ASSERT(0 == gpr_slice_cmp(slice1, slice2));

  gpr_slice_unref(slice2);
  GRPC_MDSTR_UNREF(str);
  grpc_shutdown();
}

static void test_user_data_works(void) {
  int *ud1;
  int *ud2;
  grpc_mdelem *md;
  LOG_TEST("test_user_data_works");

  grpc_init();
  ud1 = gpr_malloc(sizeof(int));
  *ud1 = 1;
  ud2 = gpr_malloc(sizeof(int));
  *ud2 = 2;
  md = grpc_mdelem_from_strings("abc", "123");
  grpc_mdelem_set_user_data(md, gpr_free, ud1);
  grpc_mdelem_set_user_data(md, gpr_free, ud2);
  GPR_ASSERT(grpc_mdelem_get_user_data(md, gpr_free) == ud1);
  GRPC_MDELEM_UNREF(md);
  grpc_shutdown();
}

static void verify_ascii_header_size(const char *key, const char *value) {
  grpc_mdelem *elem = grpc_mdelem_from_strings(key, value);
  size_t elem_size = grpc_mdelem_get_size_in_hpack_table(elem);
  size_t expected_size = 32 + strlen(key) + strlen(value);
  GPR_ASSERT(expected_size == elem_size);
  GRPC_MDELEM_UNREF(elem);
}

static void verify_binary_header_size(const char *key, const uint8_t *value,
                                      size_t value_len) {
  grpc_mdelem *elem = grpc_mdelem_from_string_and_buffer(key, value, value_len);
  GPR_ASSERT(grpc_is_binary_header(key, strlen(key)));
  size_t elem_size = grpc_mdelem_get_size_in_hpack_table(elem);
  gpr_slice value_slice =
      gpr_slice_from_copied_buffer((const char *)value, value_len);
  gpr_slice base64_encoded = grpc_chttp2_base64_encode(value_slice);
  size_t expected_size = 32 + strlen(key) + GPR_SLICE_LENGTH(base64_encoded);
  GPR_ASSERT(expected_size == elem_size);
  gpr_slice_unref(value_slice);
  gpr_slice_unref(base64_encoded);
  GRPC_MDELEM_UNREF(elem);
}

#define BUFFER_SIZE 64
static void test_mdelem_sizes_in_hpack(void) {
  LOG_TEST("test_mdelem_size");
  grpc_init();

  uint8_t binary_value[BUFFER_SIZE] = {0};
  for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
    binary_value[i] = i;
  }

  verify_ascii_header_size("hello", "world");
  verify_ascii_header_size("hello", "worldxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
  verify_ascii_header_size(":scheme", "http");

  for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
    verify_binary_header_size("hello-bin", binary_value, i);
  }

  const char *static_metadata = grpc_static_metadata_strings[0];
  memcpy(binary_value, static_metadata, strlen(static_metadata));
  verify_binary_header_size("hello-bin", binary_value, strlen(static_metadata));

  grpc_shutdown();
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
  test_user_data_works();
  test_mdelem_sizes_in_hpack();
  return 0;
}
