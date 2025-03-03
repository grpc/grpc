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

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <string.h>

#include <memory>

#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/util/string.h"
#include "test/core/test_util/test_config.h"

static int all_ok = 1;

static void expect_slice_eq(grpc_slice expected, grpc_slice slice,
                            const char* debug, int line) {
  if (!grpc_slice_eq(slice, expected)) {
    char* hs = grpc_dump_slice(slice, GPR_DUMP_HEX | GPR_DUMP_ASCII);
    char* he = grpc_dump_slice(expected, GPR_DUMP_HEX | GPR_DUMP_ASCII);
    LOG(ERROR) << "FAILED:" << line << ": " << debug << "\ngot:  " << hs
               << "\nwant: " << he;
    gpr_free(hs);
    gpr_free(he);
    all_ok = 0;
  }
  grpc_slice_unref(expected);
  grpc_slice_unref(slice);
}

static grpc_slice B64(const char* s) {
  grpc_slice ss = grpc_slice_from_copied_string(s);
  grpc_slice out = grpc_chttp2_base64_encode(ss);
  grpc_slice_unref(ss);
  return out;
}

static grpc_slice HUFF(const char* s) {
  grpc_slice ss = grpc_slice_from_copied_string(s);
  grpc_slice out = grpc_chttp2_huffman_compress(ss);
  grpc_slice_unref(ss);
  return out;
}

#define EXPECT_SLICE_EQ(expected, slice)                                    \
  expect_slice_eq(                                                          \
      grpc_slice_from_copied_buffer(expected, sizeof(expected) - 1), slice, \
      #slice, __LINE__);

static void expect_combined_equiv(const char* s, size_t len, int line) {
  grpc_slice input = grpc_slice_from_copied_buffer(s, len);
  grpc_slice base64 = grpc_chttp2_base64_encode(input);
  grpc_slice expect = grpc_chttp2_huffman_compress(base64);
  uint32_t wire_size;
  grpc_slice got =
      grpc_chttp2_base64_encode_and_huffman_compress(input, &wire_size);
  if (!grpc_slice_eq(expect, got)) {
    char* t = grpc_dump_slice(input, GPR_DUMP_HEX | GPR_DUMP_ASCII);
    char* e = grpc_dump_slice(expect, GPR_DUMP_HEX | GPR_DUMP_ASCII);
    char* g = grpc_dump_slice(got, GPR_DUMP_HEX | GPR_DUMP_ASCII);
    LOG(ERROR) << "FAILED:" << line << ":\ntest: " << t << "\ngot:  " << g
               << "\nwant: " << e;
    gpr_free(t);
    gpr_free(e);
    gpr_free(g);
    all_ok = 0;
  }
  grpc_slice_unref(input);
  grpc_slice_unref(base64);
  grpc_slice_unref(expect);
  grpc_slice_unref(got);
}

#define EXPECT_COMBINED_EQUIV(x) \
  expect_combined_equiv(x, sizeof(x) - 1, __LINE__)

static void expect_binary_header(const char* hdr, int binary) {
  if (grpc_is_binary_header(grpc_slice_from_static_string(hdr)) != binary) {
    LOG(ERROR) << "FAILED: expected header '" << hdr << "' to be "
               << (binary ? "binary" : "not binary");
    all_ok = 0;
  }
}

TEST(BinEncoderTest, MainTest) {
  // Base64 test vectors from RFC 4648, with padding removed
  // BASE64("") = ""
  EXPECT_SLICE_EQ("", B64(""));
  // BASE64("f") = "Zg"
  EXPECT_SLICE_EQ("Zg", B64("f"));
  // BASE64("fo") = "Zm8"
  EXPECT_SLICE_EQ("Zm8", B64("fo"));
  // BASE64("foo") = "Zm9v"
  EXPECT_SLICE_EQ("Zm9v", B64("foo"));
  // BASE64("foob") = "Zm9vYg"
  EXPECT_SLICE_EQ("Zm9vYg", B64("foob"));
  // BASE64("fooba") = "Zm9vYmE"
  EXPECT_SLICE_EQ("Zm9vYmE", B64("fooba"));
  // BASE64("foobar") = "Zm9vYmFy"
  EXPECT_SLICE_EQ("Zm9vYmFy", B64("foobar"));

  EXPECT_SLICE_EQ("wMHCw8TF", B64("\xc0\xc1\xc2\xc3\xc4\xc5"));

  // Huffman encoding tests
  EXPECT_SLICE_EQ("\xf1\xe3\xc2\xe5\xf2\x3a\x6b\xa0\xab\x90\xf4\xff",
                  HUFF("www.example.com"));
  EXPECT_SLICE_EQ("\xa8\xeb\x10\x64\x9c\xbf", HUFF("no-cache"));
  EXPECT_SLICE_EQ("\x25\xa8\x49\xe9\x5b\xa9\x7d\x7f", HUFF("custom-key"));
  EXPECT_SLICE_EQ("\x25\xa8\x49\xe9\x5b\xb8\xe8\xb4\xbf", HUFF("custom-value"));
  EXPECT_SLICE_EQ("\xae\xc3\x77\x1a\x4b", HUFF("private"));
  EXPECT_SLICE_EQ(
      "\xd0\x7a\xbe\x94\x10\x54\xd4\x44\xa8\x20\x05\x95\x04\x0b\x81\x66\xe0\x82"
      "\xa6\x2d\x1b\xff",
      HUFF("Mon, 21 Oct 2013 20:13:21 GMT"));
  EXPECT_SLICE_EQ(
      "\x9d\x29\xad\x17\x18\x63\xc7\x8f\x0b\x97\xc8\xe9\xae\x82\xae\x43\xd3",
      HUFF("https://www.example.com"));

  // Various test vectors for combined encoding
  EXPECT_COMBINED_EQUIV("");
  EXPECT_COMBINED_EQUIV("f");
  EXPECT_COMBINED_EQUIV("fo");
  EXPECT_COMBINED_EQUIV("foo");
  EXPECT_COMBINED_EQUIV("foob");
  EXPECT_COMBINED_EQUIV("fooba");
  EXPECT_COMBINED_EQUIV("foobar");
  EXPECT_COMBINED_EQUIV("www.example.com");
  EXPECT_COMBINED_EQUIV("no-cache");
  EXPECT_COMBINED_EQUIV("custom-key");
  EXPECT_COMBINED_EQUIV("custom-value");
  EXPECT_COMBINED_EQUIV("private");
  EXPECT_COMBINED_EQUIV("Mon, 21 Oct 2013 20:13:21 GMT");
  EXPECT_COMBINED_EQUIV("https://www.example.com");
  EXPECT_COMBINED_EQUIV(
      "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
      "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f"
      "\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f"
      "\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\x3e\x3f"
      "\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f"
      "\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5a\x5b\x5c\x5d\x5e\x5f"
      "\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6a\x6b\x6c\x6d\x6e\x6f"
      "\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7a\x7b\x7c\x7d\x7e\x7f"
      "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
      "\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f"
      "\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf"
      "\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf"
      "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf"
      "\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf"
      "\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef"
      "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff");

  expect_binary_header("foo-bin", 1);
  expect_binary_header("foo-bar", 0);
  expect_binary_header("-bin", 0);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
