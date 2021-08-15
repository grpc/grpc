/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"

#include <stdarg.h>

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/parse_hexstring.h"
#include "test/core/util/slice_splitter.h"
#include "test/core/util/test_config.h"

using MDVec = std::vector<std::pair<const char*, const char*>>;

struct TestChecker {
  MDVec* expect;

  explicit TestChecker(MDVec* expect) : expect(expect) {}

  grpc_error_handle operator()(grpc_mdelem md) {
    GPR_ASSERT(!expect->empty());
    auto e = expect->begin();
    GPR_ASSERT(grpc_slice_str_cmp(GRPC_MDKEY(md), e->first) == 0);
    GPR_ASSERT(grpc_slice_str_cmp(GRPC_MDVALUE(md), e->second) == 0);
    GRPC_MDELEM_UNREF(md);
    expect->erase(e);
    return GRPC_ERROR_NONE;
  }
};

static void test_vector(grpc_core::HPackParser* parser,
                        grpc_slice_split_mode mode, const char* hexstring,
                        MDVec expect) {
  grpc_slice input = parse_hexstring(hexstring);
  grpc_slice* slices;
  size_t nslices;
  size_t i;

  parser->BeginFrame(TestChecker(&expect),
                     grpc_core::HPackParser::Boundary::None,
                     grpc_core::HPackParser::Priority::None);

  grpc_split_slices(mode, &input, 1, &slices, &nslices);
  grpc_slice_unref(input);

  for (i = 0; i < nslices; i++) {
    grpc_core::ExecCtx exec_ctx;
    auto err = parser->Parse(slices[i], i == nslices - 1);
    if (err != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Unexpected parse error: %s", grpc_error_string(err));
      abort();
    }
  }

  for (i = 0; i < nslices; i++) {
    grpc_slice_unref(slices[i]);
  }
  gpr_free(slices);

  GPR_ASSERT(expect.empty());
}

static void test_vectors(grpc_slice_split_mode mode) {
  grpc_core::ExecCtx exec_ctx;

  {
    grpc_core::HPackParser parser;
    /* D.2.1 */
    test_vector(&parser, mode,
                "400a 6375 7374 6f6d 2d6b 6579 0d63 7573"
                "746f 6d2d 6865 6164 6572",
                {std::make_pair("custom-key", "custom-header")});
    /* D.2.2 */
    test_vector(&parser, mode, "040c 2f73 616d 706c 652f 7061 7468",
                {std::make_pair(":path", "/sample/path")});
    /* D.2.3 */
    test_vector(&parser, mode,
                "1008 7061 7373 776f 7264 0673 6563 7265"
                "74",
                {std::make_pair("password", "secret")});
    /* D.2.4 */
    test_vector(&parser, mode, "82", {std::make_pair(":method", "GET")});
  }

  {
    grpc_core::HPackParser parser;
    /* D.3.1 */
    test_vector(
        &parser, mode,
        "8286 8441 0f77 7777 2e65 7861 6d70 6c65"
        "2e63 6f6d",
        {std::make_pair(":method", "GET"), std::make_pair(":scheme", "http"),
         std::make_pair(":path", "/"),
         std::make_pair(":authority", "www.example.com")});
    /* D.3.2 */
    test_vector(
        &parser, mode, "8286 84be 5808 6e6f 2d63 6163 6865",
        {std::make_pair(":method", "GET"), std::make_pair(":scheme", "http"),
         std::make_pair(":path", "/"),
         std::make_pair(":authority", "www.example.com"),
         std::make_pair("cache-control", "no-cache")});
    /* D.3.3 */
    test_vector(&parser, mode,
                "8287 85bf 400a 6375 7374 6f6d 2d6b 6579"
                "0c63 7573 746f 6d2d 7661 6c75 65",
                {
                    std::make_pair(":method", "GET"),
                    std::make_pair(":scheme", "https"),
                    std::make_pair(":path", "/index.html"),
                    std::make_pair(":authority", "www.example.com"),
                    std::make_pair("custom-key", "custom-value"),
                });
  }

  {
    grpc_core::HPackParser parser;
    /* D.4.1 */
    test_vector(
        &parser, mode,
        "8286 8441 8cf1 e3c2 e5f2 3a6b a0ab 90f4"
        "ff",
        {std::make_pair(":method", "GET"), std::make_pair(":scheme", "http"),
         std::make_pair(":path", "/"),
         std::make_pair(":authority", "www.example.com")});
    /* D.4.2 */
    test_vector(
        &parser, mode, "8286 84be 5886 a8eb 1064 9cbf",
        {std::make_pair(":method", "GET"), std::make_pair(":scheme", "http"),
         std::make_pair(":path", "/"),
         std::make_pair(":authority", "www.example.com"),
         std::make_pair("cache-control", "no-cache")});
    /* D.4.3 */
    test_vector(
        &parser, mode,
        "8287 85bf 4088 25a8 49e9 5ba9 7d7f 8925"
        "a849 e95b b8e8 b4bf",
        {std::make_pair(":method", "GET"), std::make_pair(":scheme", "https"),
         std::make_pair(":path", "/index.html"),
         std::make_pair(":authority", "www.example.com"),
         std::make_pair("custom-key", "custom-value")});
  }

  {
    grpc_core::HPackParser parser;
    parser.hpack_table()->SetMaxBytes(256);
    parser.hpack_table()->SetCurrentTableSize(256);
    /* D.5.1 */
    test_vector(&parser, mode,
                "4803 3330 3258 0770 7269 7661 7465 611d"
                "4d6f 6e2c 2032 3120 4f63 7420 3230 3133"
                "2032 303a 3133 3a32 3120 474d 546e 1768"
                "7474 7073 3a2f 2f77 7777 2e65 7861 6d70"
                "6c65 2e63 6f6d",
                {std::make_pair(":status", "302"),
                 std::make_pair("cache-control", "private"),
                 std::make_pair("date", "Mon, 21 Oct 2013 20:13:21 GMT"),
                 std::make_pair("location", "https://www.example.com")});
    /* D.5.2 */
    test_vector(&parser, mode, "4803 3330 37c1 c0bf",
                {std::make_pair(":status", "307"),
                 std::make_pair("cache-control", "private"),
                 std::make_pair("date", "Mon, 21 Oct 2013 20:13:21 GMT"),
                 std::make_pair("location", "https://www.example.com")});
    /* D.5.3 */
    test_vector(
        &parser, mode,
        "88c1 611d 4d6f 6e2c 2032 3120 4f63 7420"
        "3230 3133 2032 303a 3133 3a32 3220 474d"
        "54c0 5a04 677a 6970 7738 666f 6f3d 4153"
        "444a 4b48 514b 425a 584f 5157 454f 5049"
        "5541 5851 5745 4f49 553b 206d 6178 2d61"
        "6765 3d33 3630 303b 2076 6572 7369 6f6e"
        "3d31",
        {std::make_pair(":status", "200"),
         std::make_pair("cache-control", "private"),
         std::make_pair("date", "Mon, 21 Oct 2013 20:13:22 GMT"),
         std::make_pair("location", "https://www.example.com"),
         std::make_pair("content-encoding", "gzip"),
         std::make_pair(
             "set-cookie",
             "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1")});
  }

  {
    grpc_core::HPackParser parser;
    parser.hpack_table()->SetMaxBytes(256);
    parser.hpack_table()->SetCurrentTableSize(256);
    /* D.6.1 */
    test_vector(&parser, mode,
                "4882 6402 5885 aec3 771a 4b61 96d0 7abe"
                "9410 54d4 44a8 2005 9504 0b81 66e0 82a6"
                "2d1b ff6e 919d 29ad 1718 63c7 8f0b 97c8"
                "e9ae 82ae 43d3",
                {std::make_pair(":status", "302"),
                 std::make_pair("cache-control", "private"),
                 std::make_pair("date", "Mon, 21 Oct 2013 20:13:21 GMT"),
                 std::make_pair("location", "https://www.example.com")});
    /* D.6.2 */
    test_vector(&parser, mode, "4883 640e ffc1 c0bf",
                {std::make_pair(":status", "307"),
                 std::make_pair("cache-control", "private"),
                 std::make_pair("date", "Mon, 21 Oct 2013 20:13:21 GMT"),
                 std::make_pair("location", "https://www.example.com")});
    /* D.6.3 */
    test_vector(
        &parser, mode,
        "88c1 6196 d07a be94 1054 d444 a820 0595"
        "040b 8166 e084 a62d 1bff c05a 839b d9ab"
        "77ad 94e7 821d d7f2 e6c7 b335 dfdf cd5b"
        "3960 d5af 2708 7f36 72c1 ab27 0fb5 291f"
        "9587 3160 65c0 03ed 4ee5 b106 3d50 07",
        {std::make_pair(":status", "200"),
         std::make_pair("cache-control", "private"),
         std::make_pair("date", "Mon, 21 Oct 2013 20:13:22 GMT"),
         std::make_pair("location", "https://www.example.com"),
         std::make_pair("content-encoding", "gzip"),
         std::make_pair(
             "set-cookie",
             "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1")});
  }

  {
    grpc_core::HPackParser parser;
    // Binary metadata: created using:
    // tools/codegen/core/gen_header_frame.py
    //    --compression inc --no_framing --hex
    //    < test/core/transport/chttp2/binary-metadata.headers
    test_vector(&parser, mode,
                "40 09 61 2e 62 2e 63 2d 62 69 6e 0c 62 32 31 6e 4d 6a 41 79 "
                "4d 51 3d 3d",
                {std::make_pair("a.b.c-bin", "omg2021")});
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_vectors(GRPC_SLICE_SPLIT_MERGE_ALL);
  test_vectors(GRPC_SLICE_SPLIT_ONE_BYTE);
  grpc_shutdown();
  return 0;
}
