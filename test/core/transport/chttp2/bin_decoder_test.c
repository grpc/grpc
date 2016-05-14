/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/ext/transport/chttp2/transport/bin_decoder.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/lib/support/string.h"

static int all_ok = 1;

static void expect_slice_eq(gpr_slice expected, gpr_slice slice, char *debug,
                            int line) {
  if (0 != gpr_slice_cmp(slice, expected)) {
    char *hs = gpr_dump_slice(slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
    char *he = gpr_dump_slice(expected, GPR_DUMP_HEX | GPR_DUMP_ASCII);
    gpr_log(GPR_ERROR, "FAILED:%d: %s\ngot:  %s\nwant: %s", line, debug, hs,
            he);
    gpr_free(hs);
    gpr_free(he);
    all_ok = 0;
  }
  gpr_slice_unref(expected);
  gpr_slice_unref(slice);
}

static gpr_slice base64_encode(const char *s) {
  gpr_slice ss = gpr_slice_from_copied_string(s);
  gpr_slice out = grpc_chttp2_base64_encode(ss);
  gpr_slice_unref(ss);
  return out;
}

static gpr_slice base64_decode(const char *s) {
  gpr_slice ss = gpr_slice_from_copied_string(s);
  gpr_slice out = grpc_chttp2_base64_decode(ss);
  gpr_slice_unref(ss);
  return out;
}

static gpr_slice base64_decode_with_length(const char *s,
                                           size_t output_length) {
  gpr_slice ss = gpr_slice_from_copied_string(s);
  gpr_slice out = grpc_chttp2_base64_decode_with_length(ss, output_length);
  gpr_slice_unref(ss);
  return out;
}

#define EXPECT_SLICE_EQ(expected, slice)                                   \
  expect_slice_eq(                                                         \
      gpr_slice_from_copied_buffer(expected, sizeof(expected) - 1), slice, \
      #slice, __LINE__);

#define ENCODE_AND_DECODE(s) \
  EXPECT_SLICE_EQ(           \
      s, grpc_chttp2_base64_decode_with_length(base64_encode(s), strlen(s)));

int main(int argc, char **argv) {
  /* ENCODE_AND_DECODE tests grpc_chttp2_base64_decode_with_length(), which
     takes encoded base64 strings without pad chars, but output length is
     required. */
  /* Base64 test vectors from RFC 4648 */
  ENCODE_AND_DECODE("");
  ENCODE_AND_DECODE("f");
  ENCODE_AND_DECODE("foo");
  ENCODE_AND_DECODE("fo");
  ENCODE_AND_DECODE("foob");
  ENCODE_AND_DECODE("fooba");
  ENCODE_AND_DECODE("foobar");

  ENCODE_AND_DECODE("\xc0\xc1\xc2\xc3\xc4\xc5");

  /* Base64 test vectors from RFC 4648, with pad chars */
  /* BASE64("") = "" */
  EXPECT_SLICE_EQ("", base64_decode(""));
  /* BASE64("f") = "Zg==" */
  EXPECT_SLICE_EQ("f", base64_decode("Zg=="));
  /* BASE64("fo") = "Zm8=" */
  EXPECT_SLICE_EQ("fo", base64_decode("Zm8="));
  /* BASE64("foo") = "Zm9v" */
  EXPECT_SLICE_EQ("foo", base64_decode("Zm9v"));
  /* BASE64("foob") = "Zm9vYg==" */
  EXPECT_SLICE_EQ("foob", base64_decode("Zm9vYg=="));
  /* BASE64("fooba") = "Zm9vYmE=" */
  EXPECT_SLICE_EQ("fooba", base64_decode("Zm9vYmE="));
  /* BASE64("foobar") = "Zm9vYmFy" */
  EXPECT_SLICE_EQ("foobar", base64_decode("Zm9vYmFy"));

  EXPECT_SLICE_EQ("\xc0\xc1\xc2\xc3\xc4\xc5", base64_decode("wMHCw8TF"));

  // Test illegal input length in grpc_chttp2_base64_decode
  EXPECT_SLICE_EQ("", base64_decode("a"));
  EXPECT_SLICE_EQ("", base64_decode("ab"));
  EXPECT_SLICE_EQ("", base64_decode("abc"));

  // Test illegal charactors in grpc_chttp2_base64_decode
  EXPECT_SLICE_EQ("", base64_decode("Zm:v"));
  EXPECT_SLICE_EQ("", base64_decode("Zm=v"));

  // Test output_length longer than max possible output length in
  // grpc_chttp2_base64_decode_with_length
  EXPECT_SLICE_EQ("", base64_decode_with_length("Zg", 2));
  EXPECT_SLICE_EQ("", base64_decode_with_length("Zm8", 3));
  EXPECT_SLICE_EQ("", base64_decode_with_length("Zm9v", 4));

  // Test illegal charactors in grpc_chttp2_base64_decode_with_length
  EXPECT_SLICE_EQ("", base64_decode_with_length("Zm:v", 3));
  EXPECT_SLICE_EQ("", base64_decode_with_length("Zm=v", 3));

  return all_ok ? 0 : 1;
}
