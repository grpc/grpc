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

#include "src/core/lib/slice/b64.h"

#include <string.h>

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

static int buffers_are_equal(const unsigned char *buf1,
                             const unsigned char *buf2, size_t size) {
  size_t i;
  for (i = 0; i < size; i++) {
    if (buf1[i] != buf2[i]) {
      gpr_log(GPR_ERROR, "buf1 and buf2 differ: buf1[%d] = %x vs buf2[%d] = %x",
              (int)i, buf1[i], (int)i, buf2[i]);
      return 0;
    }
  }
  return 1;
}

static void test_simple_encode_decode_b64(int url_safe, int multiline) {
  const char *hello = "hello";
  char *hello_b64 =
      grpc_base64_encode(hello, strlen(hello), url_safe, multiline);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_slice hello_slice = grpc_base64_decode(&exec_ctx, hello_b64, url_safe);
  GPR_ASSERT(GRPC_SLICE_LENGTH(hello_slice) == strlen(hello));
  GPR_ASSERT(strncmp((const char *)GRPC_SLICE_START_PTR(hello_slice), hello,
                     GRPC_SLICE_LENGTH(hello_slice)) == 0);

  grpc_slice_unref_internal(&exec_ctx, hello_slice);
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_free(hello_b64);
}

static void test_full_range_encode_decode_b64(int url_safe, int multiline) {
  unsigned char orig[256];
  size_t i;
  char *b64;
  grpc_slice orig_decoded;
  for (i = 0; i < sizeof(orig); i++) orig[i] = (uint8_t)i;

  /* Try all the different paddings. */
  for (i = 0; i < 3; i++) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    b64 = grpc_base64_encode(orig, sizeof(orig) - i, url_safe, multiline);
    orig_decoded = grpc_base64_decode(&exec_ctx, b64, url_safe);
    GPR_ASSERT(GRPC_SLICE_LENGTH(orig_decoded) == (sizeof(orig) - i));
    GPR_ASSERT(buffers_are_equal(orig, GRPC_SLICE_START_PTR(orig_decoded),
                                 sizeof(orig) - i));
    grpc_slice_unref_internal(&exec_ctx, orig_decoded);
    gpr_free(b64);
    grpc_exec_ctx_finish(&exec_ctx);
  }
}

static void test_simple_encode_decode_b64_no_multiline(void) {
  test_simple_encode_decode_b64(0, 0);
}

static void test_simple_encode_decode_b64_multiline(void) {
  test_simple_encode_decode_b64(0, 1);
}

static void test_simple_encode_decode_b64_urlsafe_no_multiline(void) {
  test_simple_encode_decode_b64(1, 0);
}

static void test_simple_encode_decode_b64_urlsafe_multiline(void) {
  test_simple_encode_decode_b64(1, 1);
}

static void test_full_range_encode_decode_b64_no_multiline(void) {
  test_full_range_encode_decode_b64(0, 0);
}

static void test_full_range_encode_decode_b64_multiline(void) {
  test_full_range_encode_decode_b64(0, 1);
}

static void test_full_range_encode_decode_b64_urlsafe_no_multiline(void) {
  test_full_range_encode_decode_b64(1, 0);
}

static void test_full_range_encode_decode_b64_urlsafe_multiline(void) {
  test_full_range_encode_decode_b64(1, 1);
}

static void test_url_safe_unsafe_mismatch_failure(void) {
  unsigned char orig[256];
  size_t i;
  char *b64;
  grpc_slice orig_decoded;
  int url_safe = 1;
  for (i = 0; i < sizeof(orig); i++) orig[i] = (uint8_t)i;

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  b64 = grpc_base64_encode(orig, sizeof(orig), url_safe, 0);
  orig_decoded = grpc_base64_decode(&exec_ctx, b64, !url_safe);
  GPR_ASSERT(GRPC_SLICE_IS_EMPTY(orig_decoded));
  gpr_free(b64);
  grpc_slice_unref_internal(&exec_ctx, orig_decoded);

  b64 = grpc_base64_encode(orig, sizeof(orig), !url_safe, 0);
  orig_decoded = grpc_base64_decode(&exec_ctx, b64, url_safe);
  GPR_ASSERT(GRPC_SLICE_IS_EMPTY(orig_decoded));
  gpr_free(b64);
  grpc_slice_unref_internal(&exec_ctx, orig_decoded);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_rfc4648_test_vectors(void) {
  char *b64;

  b64 = grpc_base64_encode("", 0, 0, 0);
  GPR_ASSERT(strcmp("", b64) == 0);
  gpr_free(b64);

  b64 = grpc_base64_encode("f", 1, 0, 0);
  GPR_ASSERT(strcmp("Zg==", b64) == 0);
  gpr_free(b64);

  b64 = grpc_base64_encode("fo", 2, 0, 0);
  GPR_ASSERT(strcmp("Zm8=", b64) == 0);
  gpr_free(b64);

  b64 = grpc_base64_encode("foo", 3, 0, 0);
  GPR_ASSERT(strcmp("Zm9v", b64) == 0);
  gpr_free(b64);

  b64 = grpc_base64_encode("foob", 4, 0, 0);
  GPR_ASSERT(strcmp("Zm9vYg==", b64) == 0);
  gpr_free(b64);

  b64 = grpc_base64_encode("fooba", 5, 0, 0);
  GPR_ASSERT(strcmp("Zm9vYmE=", b64) == 0);
  gpr_free(b64);

  b64 = grpc_base64_encode("foobar", 6, 0, 0);
  GPR_ASSERT(strcmp("Zm9vYmFy", b64) == 0);
  gpr_free(b64);
}

static void test_unpadded_decode(void) {
  grpc_slice decoded;

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  decoded = grpc_base64_decode(&exec_ctx, "Zm9vYmFy", 0);
  GPR_ASSERT(!GRPC_SLICE_IS_EMPTY(decoded));
  GPR_ASSERT(grpc_slice_str_cmp(decoded, "foobar") == 0);
  grpc_slice_unref(decoded);

  decoded = grpc_base64_decode(&exec_ctx, "Zm9vYmE", 0);
  GPR_ASSERT(!GRPC_SLICE_IS_EMPTY(decoded));
  GPR_ASSERT(grpc_slice_str_cmp(decoded, "fooba") == 0);
  grpc_slice_unref(decoded);

  decoded = grpc_base64_decode(&exec_ctx, "Zm9vYg", 0);
  GPR_ASSERT(!GRPC_SLICE_IS_EMPTY(decoded));
  GPR_ASSERT(grpc_slice_str_cmp(decoded, "foob") == 0);
  grpc_slice_unref(decoded);

  decoded = grpc_base64_decode(&exec_ctx, "Zm9v", 0);
  GPR_ASSERT(!GRPC_SLICE_IS_EMPTY(decoded));
  GPR_ASSERT(grpc_slice_str_cmp(decoded, "foo") == 0);
  grpc_slice_unref(decoded);

  decoded = grpc_base64_decode(&exec_ctx, "Zm8", 0);
  GPR_ASSERT(!GRPC_SLICE_IS_EMPTY(decoded));
  GPR_ASSERT(grpc_slice_str_cmp(decoded, "fo") == 0);
  grpc_slice_unref(decoded);

  decoded = grpc_base64_decode(&exec_ctx, "Zg", 0);
  GPR_ASSERT(!GRPC_SLICE_IS_EMPTY(decoded));
  GPR_ASSERT(grpc_slice_str_cmp(decoded, "f") == 0);
  grpc_slice_unref(decoded);

  decoded = grpc_base64_decode(&exec_ctx, "", 0);
  GPR_ASSERT(GRPC_SLICE_IS_EMPTY(decoded));
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_simple_encode_decode_b64_no_multiline();
  test_simple_encode_decode_b64_multiline();
  test_simple_encode_decode_b64_urlsafe_no_multiline();
  test_simple_encode_decode_b64_urlsafe_multiline();
  test_full_range_encode_decode_b64_no_multiline();
  test_full_range_encode_decode_b64_multiline();
  test_full_range_encode_decode_b64_urlsafe_no_multiline();
  test_full_range_encode_decode_b64_urlsafe_multiline();
  test_url_safe_unsafe_mismatch_failure();
  test_rfc4648_test_vectors();
  test_unpadded_decode();
  return 0;
}
