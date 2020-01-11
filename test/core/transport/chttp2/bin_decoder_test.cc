/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/bin_decoder.h"

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "test/core/util/test_config.h"

static int all_ok = 1;

static void expect_slice_eq(grpc_slice expected, grpc_slice slice,
                            const char* debug, int line) {
  if (!grpc_slice_eq(slice, expected)) {
    char* hs = grpc_dump_slice(slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
    char* he = grpc_dump_slice(expected, GPR_DUMP_HEX | GPR_DUMP_ASCII);
    gpr_log(GPR_ERROR, "FAILED:%d: %s\ngot:  %s\nwant: %s", line, debug, hs,
            he);
    gpr_free(hs);
    gpr_free(he);
    all_ok = 0;
  }
  grpc_slice_unref_internal(expected);
  grpc_slice_unref_internal(slice);
}

static grpc_slice base64_encode(const char* s) {
  grpc_slice ss = grpc_slice_from_copied_string(s);
  grpc_slice out = grpc_chttp2_base64_encode(ss);
  grpc_slice_unref_internal(ss);
  return out;
}

static grpc_slice base64_decode(const char* s) {
  grpc_slice ss = grpc_slice_from_copied_string(s);
  grpc_slice out = grpc_chttp2_base64_decode(ss);
  grpc_slice_unref_internal(ss);
  return out;
}

static grpc_slice base64_decode_with_length(const char* s,
                                            size_t output_length) {
  grpc_slice ss = grpc_slice_from_copied_string(s);
  grpc_slice out = grpc_chttp2_base64_decode_with_length(ss, output_length);
  grpc_slice_unref_internal(ss);
  return out;
}

static size_t base64_infer_length(const char* s) {
  grpc_slice ss = grpc_slice_from_copied_string(s);
  size_t out = grpc_chttp2_base64_infer_length_after_decode(ss);
  grpc_slice_unref_internal(ss);
  return out;
}

#define EXPECT_DECODED_LENGTH(s, expected) \
  GPR_ASSERT((expected) == base64_infer_length((s)));

#define EXPECT_SLICE_EQ(expected, slice)                                    \
  expect_slice_eq(                                                          \
      grpc_slice_from_copied_buffer(expected, sizeof(expected) - 1), slice, \
      #slice, __LINE__);

#define ENCODE_AND_DECODE(s) \
  EXPECT_SLICE_EQ(           \
      s, grpc_chttp2_base64_decode_with_length(base64_encode(s), strlen(s)));

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;

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

    EXPECT_DECODED_LENGTH("", 0);
    EXPECT_DECODED_LENGTH("ab", 1);
    EXPECT_DECODED_LENGTH("abc", 2);
    EXPECT_DECODED_LENGTH("abcd", 3);
    EXPECT_DECODED_LENGTH("abcdef", 4);
    EXPECT_DECODED_LENGTH("abcdefg", 5);
    EXPECT_DECODED_LENGTH("abcdefgh", 6);

    EXPECT_DECODED_LENGTH("ab==", 1);
    EXPECT_DECODED_LENGTH("abc=", 2);
    EXPECT_DECODED_LENGTH("abcd", 3);
    EXPECT_DECODED_LENGTH("abcdef==", 4);
    EXPECT_DECODED_LENGTH("abcdefg=", 5);
    EXPECT_DECODED_LENGTH("abcdefgh", 6);

    EXPECT_DECODED_LENGTH("a", 0);
    EXPECT_DECODED_LENGTH("a===", 0);
    EXPECT_DECODED_LENGTH("abcde", 0);
    EXPECT_DECODED_LENGTH("abcde===", 0);
  }
  grpc_shutdown();
  return all_ok ? 0 : 1;
}
