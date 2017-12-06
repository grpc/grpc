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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"

static int all_ok = 1;

static void expect_slice_eq(grpc_exec_ctx* exec_ctx, grpc_slice expected,
                            grpc_slice slice, const char* debug, int line) {
  if (!grpc_slice_eq(slice, expected)) {
    char* hs = grpc_dump_slice(slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
    char* he = grpc_dump_slice(expected, GPR_DUMP_HEX | GPR_DUMP_ASCII);
    gpr_log(GPR_ERROR, "FAILED:%d: %s\ngot:  %s\nwant: %s", line, debug, hs,
            he);
    gpr_free(hs);
    gpr_free(he);
    all_ok = 0;
  }
  grpc_slice_unref_internal(exec_ctx, expected);
  grpc_slice_unref_internal(exec_ctx, slice);
}

static grpc_slice base64_encode(grpc_exec_ctx* exec_ctx, const char* s) {
  grpc_slice ss = grpc_slice_from_copied_string(s);
  grpc_slice out = grpc_chttp2_base64_encode(ss);
  grpc_slice_unref_internal(exec_ctx, ss);
  return out;
}

static grpc_slice base64_decode(grpc_exec_ctx* exec_ctx, const char* s) {
  grpc_slice ss = grpc_slice_from_copied_string(s);
  grpc_slice out = grpc_chttp2_base64_decode(exec_ctx, ss);
  grpc_slice_unref_internal(exec_ctx, ss);
  return out;
}

static grpc_slice base64_decode_with_length(grpc_exec_ctx* exec_ctx,
                                            const char* s,
                                            size_t output_length) {
  grpc_slice ss = grpc_slice_from_copied_string(s);
  grpc_slice out =
      grpc_chttp2_base64_decode_with_length(exec_ctx, ss, output_length);
  grpc_slice_unref_internal(exec_ctx, ss);
  return out;
}

#define EXPECT_SLICE_EQ(exec_ctx, expected, slice)                             \
  expect_slice_eq(                                                             \
      exec_ctx, grpc_slice_from_copied_buffer(expected, sizeof(expected) - 1), \
      slice, #slice, __LINE__);

#define ENCODE_AND_DECODE(exec_ctx, s)                   \
  EXPECT_SLICE_EQ(exec_ctx, s,                           \
                  grpc_chttp2_base64_decode_with_length( \
                      exec_ctx, base64_encode(exec_ctx, s), strlen(s)));

int main(int argc, char** argv) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  /* ENCODE_AND_DECODE tests grpc_chttp2_base64_decode_with_length(), which
     takes encoded base64 strings without pad chars, but output length is
     required. */
  /* Base64 test vectors from RFC 4648 */
  ENCODE_AND_DECODE(&exec_ctx, "");
  ENCODE_AND_DECODE(&exec_ctx, "f");
  ENCODE_AND_DECODE(&exec_ctx, "foo");
  ENCODE_AND_DECODE(&exec_ctx, "fo");
  ENCODE_AND_DECODE(&exec_ctx, "foob");
  ENCODE_AND_DECODE(&exec_ctx, "fooba");
  ENCODE_AND_DECODE(&exec_ctx, "foobar");

  ENCODE_AND_DECODE(&exec_ctx, "\xc0\xc1\xc2\xc3\xc4\xc5");

  /* Base64 test vectors from RFC 4648, with pad chars */
  /* BASE64("") = "" */
  EXPECT_SLICE_EQ(&exec_ctx, "", base64_decode(&exec_ctx, ""));
  /* BASE64("f") = "Zg==" */
  EXPECT_SLICE_EQ(&exec_ctx, "f", base64_decode(&exec_ctx, "Zg=="));
  /* BASE64("fo") = "Zm8=" */
  EXPECT_SLICE_EQ(&exec_ctx, "fo", base64_decode(&exec_ctx, "Zm8="));
  /* BASE64("foo") = "Zm9v" */
  EXPECT_SLICE_EQ(&exec_ctx, "foo", base64_decode(&exec_ctx, "Zm9v"));
  /* BASE64("foob") = "Zm9vYg==" */
  EXPECT_SLICE_EQ(&exec_ctx, "foob", base64_decode(&exec_ctx, "Zm9vYg=="));
  /* BASE64("fooba") = "Zm9vYmE=" */
  EXPECT_SLICE_EQ(&exec_ctx, "fooba", base64_decode(&exec_ctx, "Zm9vYmE="));
  /* BASE64("foobar") = "Zm9vYmFy" */
  EXPECT_SLICE_EQ(&exec_ctx, "foobar", base64_decode(&exec_ctx, "Zm9vYmFy"));

  EXPECT_SLICE_EQ(&exec_ctx, "\xc0\xc1\xc2\xc3\xc4\xc5",
                  base64_decode(&exec_ctx, "wMHCw8TF"));

  // Test illegal input length in grpc_chttp2_base64_decode
  EXPECT_SLICE_EQ(&exec_ctx, "", base64_decode(&exec_ctx, "a"));
  EXPECT_SLICE_EQ(&exec_ctx, "", base64_decode(&exec_ctx, "ab"));
  EXPECT_SLICE_EQ(&exec_ctx, "", base64_decode(&exec_ctx, "abc"));

  // Test illegal charactors in grpc_chttp2_base64_decode
  EXPECT_SLICE_EQ(&exec_ctx, "", base64_decode(&exec_ctx, "Zm:v"));
  EXPECT_SLICE_EQ(&exec_ctx, "", base64_decode(&exec_ctx, "Zm=v"));

  // Test output_length longer than max possible output length in
  // grpc_chttp2_base64_decode_with_length
  EXPECT_SLICE_EQ(&exec_ctx, "", base64_decode_with_length(&exec_ctx, "Zg", 2));
  EXPECT_SLICE_EQ(&exec_ctx, "",
                  base64_decode_with_length(&exec_ctx, "Zm8", 3));
  EXPECT_SLICE_EQ(&exec_ctx, "",
                  base64_decode_with_length(&exec_ctx, "Zm9v", 4));

  // Test illegal charactors in grpc_chttp2_base64_decode_with_length
  EXPECT_SLICE_EQ(&exec_ctx, "",
                  base64_decode_with_length(&exec_ctx, "Zm:v", 3));
  EXPECT_SLICE_EQ(&exec_ctx, "",
                  base64_decode_with_length(&exec_ctx, "Zm=v", 3));

  grpc_exec_ctx_finish(&exec_ctx);

  return all_ok ? 0 : 1;
}
