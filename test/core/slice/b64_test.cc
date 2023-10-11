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

#include "src/core/lib/slice/b64.h"

#include <stdint.h>
#include <string.h>

#include <memory>

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include <grpc/slice.h>
#include <grpc/support/alloc.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

static void test_simple_encode_decode_b64(int url_safe, int multiline) {
  const char* hello = "hello";
  char* hello_b64 =
      grpc_base64_encode(hello, strlen(hello), url_safe, multiline);
  grpc_core::ExecCtx exec_ctx;
  grpc_slice hello_slice = grpc_base64_decode(hello_b64, url_safe);
  ASSERT_EQ(grpc_core::StringViewFromSlice(hello_slice),
            absl::string_view(hello));
  grpc_slice_unref(hello_slice);

  gpr_free(hello_b64);
}

static void test_full_range_encode_decode_b64(int url_safe, int multiline) {
  unsigned char orig[256];
  size_t i;
  char* b64;
  grpc_slice orig_decoded;
  for (i = 0; i < sizeof(orig); i++) orig[i] = static_cast<uint8_t>(i);

  // Try all the different paddings.
  for (i = 0; i < 3; i++) {
    grpc_core::ExecCtx exec_ctx;
    b64 = grpc_base64_encode(orig, sizeof(orig) - i, url_safe, multiline);
    orig_decoded = grpc_base64_decode(b64, url_safe);
    ASSERT_EQ(
        grpc_core::StringViewFromSlice(orig_decoded),
        absl::string_view(reinterpret_cast<char*>(orig), sizeof(orig) - i));
    grpc_slice_unref(orig_decoded);
    gpr_free(b64);
  }
}

TEST(B64Test, SimpleEncodeDecodeB64NoMultiline) {
  test_simple_encode_decode_b64(0, 0);
}

TEST(B64Test, SimpleEncodeDecodeB64Multiline) {
  test_simple_encode_decode_b64(0, 1);
}

TEST(B64Test, SimpleEncodeDecodeB64UrlsafeNoMultiline) {
  test_simple_encode_decode_b64(1, 0);
}

TEST(B64Test, SimpleEncodeDecodeB64UrlsafeMultiline) {
  test_simple_encode_decode_b64(1, 1);
}

TEST(B64Test, FullRangeEncodeDecodeB64NoMultiline) {
  test_full_range_encode_decode_b64(0, 0);
}

TEST(B64Test, FullRangeEncodeDecodeB64Multiline) {
  test_full_range_encode_decode_b64(0, 1);
}

TEST(B64Test, FullRangeEncodeDecodeB64UrlsafeNoMultiline) {
  test_full_range_encode_decode_b64(1, 0);
}

TEST(B64Test, FullRangeEncodeDecodeB64UrlsafeMultiline) {
  test_full_range_encode_decode_b64(1, 1);
}

TEST(B64Test, UrlSafeUnsafeMismatchFailure) {
  unsigned char orig[256];
  size_t i;
  char* b64;
  grpc_slice orig_decoded;
  int url_safe = 1;
  for (i = 0; i < sizeof(orig); i++) orig[i] = static_cast<uint8_t>(i);

  grpc_core::ExecCtx exec_ctx;
  b64 = grpc_base64_encode(orig, sizeof(orig), url_safe, 0);
  orig_decoded = grpc_base64_decode(b64, !url_safe);
  ASSERT_TRUE(GRPC_SLICE_IS_EMPTY(orig_decoded));
  gpr_free(b64);
  grpc_slice_unref(orig_decoded);

  b64 = grpc_base64_encode(orig, sizeof(orig), !url_safe, 0);
  orig_decoded = grpc_base64_decode(b64, url_safe);
  ASSERT_TRUE(GRPC_SLICE_IS_EMPTY(orig_decoded));
  gpr_free(b64);
  grpc_slice_unref(orig_decoded);
}

TEST(B64Test, Rfc4648TestVectors) {
  char* b64;

  b64 = grpc_base64_encode("", 0, 0, 0);
  ASSERT_STREQ("", b64);
  gpr_free(b64);

  b64 = grpc_base64_encode("f", 1, 0, 0);
  ASSERT_STREQ("Zg==", b64);
  gpr_free(b64);

  b64 = grpc_base64_encode("fo", 2, 0, 0);
  ASSERT_STREQ("Zm8=", b64);
  gpr_free(b64);

  b64 = grpc_base64_encode("foo", 3, 0, 0);
  ASSERT_STREQ("Zm9v", b64);
  gpr_free(b64);

  b64 = grpc_base64_encode("foob", 4, 0, 0);
  ASSERT_STREQ("Zm9vYg==", b64);
  gpr_free(b64);

  b64 = grpc_base64_encode("fooba", 5, 0, 0);
  ASSERT_STREQ("Zm9vYmE=", b64);
  gpr_free(b64);

  b64 = grpc_base64_encode("foobar", 6, 0, 0);
  ASSERT_STREQ("Zm9vYmFy", b64);
  gpr_free(b64);
}

TEST(B64Test, UnpaddedDecode) {
  grpc_slice decoded;

  grpc_core::ExecCtx exec_ctx;
  decoded = grpc_base64_decode("Zm9vYmFy", 0);

  ASSERT_EQ(grpc_core::StringViewFromSlice(decoded), "foobar");
  grpc_slice_unref(decoded);

  decoded = grpc_base64_decode("Zm9vYmE", 0);
  ASSERT_EQ(grpc_core::StringViewFromSlice(decoded), "fooba");
  grpc_slice_unref(decoded);

  decoded = grpc_base64_decode("Zm9vYg", 0);
  ASSERT_EQ(grpc_core::StringViewFromSlice(decoded), "foob");
  grpc_slice_unref(decoded);

  decoded = grpc_base64_decode("Zm9v", 0);
  ASSERT_EQ(grpc_core::StringViewFromSlice(decoded), "foo");
  grpc_slice_unref(decoded);

  decoded = grpc_base64_decode("Zm8", 0);
  ASSERT_EQ(grpc_core::StringViewFromSlice(decoded), "fo");
  grpc_slice_unref(decoded);

  decoded = grpc_base64_decode("Zg", 0);
  ASSERT_EQ(grpc_core::StringViewFromSlice(decoded), "f");
  grpc_slice_unref(decoded);

  decoded = grpc_base64_decode("", 0);
  ASSERT_TRUE(GRPC_SLICE_IS_EMPTY(decoded));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
