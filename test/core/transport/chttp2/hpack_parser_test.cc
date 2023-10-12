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

#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"

#include <memory>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/error_utils.h"
#include "test/core/util/parse_hexstring.h"
#include "test/core/util/slice_splitter.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

const uint32_t kFailureIsConnectionError = 1;
const uint32_t kWithPriority = 2;
const uint32_t kEndOfStream = 4;
const uint32_t kEndOfHeaders = 8;

struct TestInput {
  std::string input;
  absl::StatusOr<std::string> expected_parse;
  uint32_t flags;
};

struct Test {
  std::string name;
  absl::optional<size_t> table_size;
  absl::optional<size_t> max_metadata_size;
  std::vector<TestInput> inputs;
};

// Produce a nice name to print next to each test case for gtest.
inline const char* NameFromConfig(
    const ::testing::TestParamInfo<Test>& config) {
  return config.param.name.c_str();
}

class ParseTest : public ::testing::TestWithParam<Test> {
 public:
  ParseTest() { grpc_init(); }

  ~ParseTest() override {
    {
      ExecCtx exec_ctx;
      parser_.reset();
    }

    grpc_shutdown();
  }

  void SetUp() override {
    parser_ = std::make_unique<HPackParser>();
    if (GetParam().table_size.has_value()) {
      parser_->hpack_table()->SetMaxBytes(GetParam().table_size.value());
      EXPECT_TRUE(parser_->hpack_table()->SetCurrentTableSize(
          GetParam().table_size.value()));
    }
  }

  static bool IsStreamError(const absl::Status& status) {
    intptr_t stream_id;
    return grpc_error_get_int(status, StatusIntProperty::kStreamId, &stream_id);
  }

  void TestVector(grpc_slice_split_mode mode,
                  absl::optional<size_t> max_metadata_size,
                  std::string hexstring, absl::StatusOr<std::string> expect,
                  uint32_t flags) {
    MemoryAllocator memory_allocator = MemoryAllocator(
        ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
            "test"));
    auto arena = MakeScopedArena(1024, &memory_allocator);
    ExecCtx exec_ctx;
    auto input = ParseHexstring(hexstring);
    grpc_slice* slices;
    size_t nslices;
    size_t i;
    absl::BitGen bitgen;

    grpc_metadata_batch b(arena.get());

    parser_->BeginFrame(
        &b, max_metadata_size.value_or(4096), max_metadata_size.value_or(4096),
        (flags & kEndOfStream)
            ? HPackParser::Boundary::EndOfStream
            : ((flags & kEndOfHeaders) ? HPackParser::Boundary::EndOfHeaders
                                       : HPackParser::Boundary::None),
        flags & kWithPriority ? HPackParser::Priority::Included
                              : HPackParser::Priority::None,
        HPackParser::LogInfo{1, HPackParser::LogInfo::kHeaders, false});

    grpc_split_slices(mode, const_cast<grpc_slice*>(&input.c_slice()), 1,
                      &slices, &nslices);
    auto cleanup_slices = absl::MakeCleanup([slices, nslices] {
      for (size_t i = 0; i < nslices; i++) {
        grpc_slice_unref(slices[i]);
      }
      gpr_free(slices);
    });

    bool saw_error = false;
    for (i = 0; i < nslices; i++) {
      ExecCtx exec_ctx;
      auto err =
          parser_->Parse(slices[i], i == nslices - 1, absl::BitGenRef(bitgen),
                         /*call_tracer=*/nullptr);
      if (!err.ok() && (flags & kFailureIsConnectionError) == 0) {
        EXPECT_TRUE(IsStreamError(err)) << err;
      }
      if (!saw_error && !err.ok()) {
        // one byte at a time mode might fail with a stream error early
        if (mode == GRPC_SLICE_SPLIT_ONE_BYTE &&
            (flags & kFailureIsConnectionError) && IsStreamError(err)) {
          continue;
        }
        grpc_status_code code;
        std::string message;
        grpc_error_get_status(err, Timestamp::InfFuture(), &code, &message,
                              nullptr, nullptr);
        EXPECT_EQ(code, static_cast<grpc_status_code>(expect.status().code()))
            << err << " slice[" << i << "]; input: " << hexstring
            << "\nTABLE:\n"
            << parser_->hpack_table()->TestOnlyDynamicTableAsString();
        EXPECT_THAT(message, ::testing::HasSubstr(expect.status().message()))
            << err << " slice[" << i << "]; input: " << hexstring
            << "\nTABLE:\n"
            << parser_->hpack_table()->TestOnlyDynamicTableAsString();
        saw_error = true;
        if (flags & kFailureIsConnectionError) return;
      }
    }

    if (!saw_error) {
      EXPECT_TRUE(expect.ok()) << expect.status();
    }

    if (expect.ok()) {
      TestEncoder encoder;
      b.Encode(&encoder);
      EXPECT_EQ(encoder.result(), *expect) << "Input: " << hexstring;
    }
  }

 private:
  class TestEncoder {
   public:
    std::string result() { return out_; }

    void Encode(const Slice& key, const Slice& value) {
      out_.append(absl::StrCat(key.as_string_view(), ": ",
                               value.as_string_view(), "\n"));
    }

    template <typename T, typename V>
    void Encode(T, const V& v) {
      out_.append(
          absl::StrCat(T::key(), ": ", T::Encode(v).as_string_view(), "\n"));
    }

   private:
    std::string out_;
  };

  std::unique_ptr<HPackParser> parser_;
};

TEST_P(ParseTest, WholeSlices) {
  for (const auto& input : GetParam().inputs) {
    TestVector(GRPC_SLICE_SPLIT_MERGE_ALL, GetParam().max_metadata_size,
               input.input, input.expected_parse, input.flags);
  }
}

TEST_P(ParseTest, OneByteAtATime) {
  for (const auto& input : GetParam().inputs) {
    TestVector(GRPC_SLICE_SPLIT_ONE_BYTE, GetParam().max_metadata_size,
               input.input, input.expected_parse, input.flags);
  }
}

INSTANTIATE_TEST_SUITE_P(
    ParseTest, ParseTest,
    ::testing::Values(
        Test{"RfcTestD2",
             {},
             {},
             {
                 // D.2.1
                 {"400a 6375 7374 6f6d 2d6b 6579 0d63 7573"
                  "746f 6d2d 6865 6164 6572",
                  "custom-key: custom-header\n", 0},
                 // D.2.2
                 {"040c 2f73 616d 706c 652f 7061 7468", ":path: /sample/path\n",
                  0},
                 // D.2.3
                 {"1008 7061 7373 776f 7264 0673 6563 7265"
                  "74",
                  "password: secret\n", 0},
                 // D.2.4
                 {"82", ":method: GET\n", 0},
             }},
        Test{"RfcTestD3",
             {},
             {},
             {
                 // D.3.1
                 {"8286 8441 0f77 7777 2e65 7861 6d70 6c65"
                  "2e63 6f6d",
                  ":path: /\n"
                  ":authority: www.example.com\n"
                  ":method: GET\n"
                  ":scheme: http\n",
                  0},
                 // D.3.2
                 {"8286 84be 5808 6e6f 2d63 6163 6865",
                  ":path: /\n"
                  ":authority: www.example.com\n"
                  ":method: GET\n"
                  ":scheme: http\n"
                  "cache-control: no-cache\n",
                  0},
                 // D.3.3
                 {"8287 85bf 400a 6375 7374 6f6d 2d6b 6579"
                  "0c63 7573 746f 6d2d 7661 6c75 65",
                  ":path: /index.html\n"
                  ":authority: www.example.com\n"
                  ":method: GET\n"
                  ":scheme: https\n"
                  "custom-key: custom-value\n",
                  0},
             }},
        Test{"RfcTestD4",
             {},
             {},
             {
                 // D.4.1
                 {"8286 8441 8cf1 e3c2 e5f2 3a6b a0ab 90f4"
                  "ff",
                  ":path: /\n"
                  ":authority: www.example.com\n"
                  ":method: GET\n"
                  ":scheme: http\n",
                  0},
                 // D.4.2
                 {"8286 84be 5886 a8eb 1064 9cbf",
                  ":path: /\n"
                  ":authority: www.example.com\n"
                  ":method: GET\n"
                  ":scheme: http\n"
                  "cache-control: no-cache\n",
                  0},
                 // D.4.3
                 {"8287 85bf 4088 25a8 49e9 5ba9 7d7f 8925"
                  "a849 e95b b8e8 b4bf",
                  ":path: /index.html\n"
                  ":authority: www.example.com\n"
                  ":method: GET\n"
                  ":scheme: https\n"
                  "custom-key: custom-value\n",
                  0},
             }},
        Test{"RfcTestD5",
             {256},
             {},
             {
                 // D.5.1
                 {"4803 3330 3258 0770 7269 7661 7465 611d"
                  "4d6f 6e2c 2032 3120 4f63 7420 3230 3133"
                  "2032 303a 3133 3a32 3120 474d 546e 1768"
                  "7474 7073 3a2f 2f77 7777 2e65 7861 6d70"
                  "6c65 2e63 6f6d",
                  ":status: 302\n"
                  "cache-control: private\n"
                  "date: Mon, 21 Oct 2013 20:13:21 GMT\n"
                  "location: https://www.example.com\n",
                  0},
                 // D.5.2
                 {"4803 3330 37c1 c0bf",
                  ":status: 307\n"
                  "cache-control: private\n"
                  "date: Mon, 21 Oct 2013 20:13:21 GMT\n"
                  "location: https://www.example.com\n",
                  0},
                 // D.5.3
                 {"88c1 611d 4d6f 6e2c 2032 3120 4f63 7420"
                  "3230 3133 2032 303a 3133 3a32 3220 474d"
                  "54c0 5a04 677a 6970 7738 666f 6f3d 4153"
                  "444a 4b48 514b 425a 584f 5157 454f 5049"
                  "5541 5851 5745 4f49 553b 206d 6178 2d61"
                  "6765 3d33 3630 303b 2076 6572 7369 6f6e"
                  "3d31",
                  ":status: 200\n"
                  "cache-control: private\n"
                  "date: Mon, 21 Oct 2013 20:13:22 GMT\n"
                  "location: https://www.example.com\n"
                  "content-encoding: gzip\n"
                  "set-cookie: foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; "
                  "version=1\n",
                  0},
             }},
        Test{"RfcTestD6",
             {256},
             {},
             {
                 // D.6.1
                 {"4882 6402 5885 aec3 771a 4b61 96d0 7abe"
                  "9410 54d4 44a8 2005 9504 0b81 66e0 82a6"
                  "2d1b ff6e 919d 29ad 1718 63c7 8f0b 97c8"
                  "e9ae 82ae 43d3",
                  ":status: 302\n"
                  "cache-control: private\n"
                  "date: Mon, 21 Oct 2013 20:13:21 GMT\n"
                  "location: https://www.example.com\n",
                  0},
                 // D.6.2
                 {"4883 640e ffc1 c0bf",
                  ":status: 307\n"
                  "cache-control: private\n"
                  "date: Mon, 21 Oct 2013 20:13:21 GMT\n"
                  "location: https://www.example.com\n",
                  0},
                 // D.6.3
                 {"88c1 6196 d07a be94 1054 d444 a820 0595"
                  "040b 8166 e084 a62d 1bff c05a 839b d9ab"
                  "77ad 94e7 821d d7f2 e6c7 b335 dfdf cd5b"
                  "3960 d5af 2708 7f36 72c1 ab27 0fb5 291f"
                  "9587 3160 65c0 03ed 4ee5 b106 3d50 07",
                  ":status: 200\n"
                  "cache-control: private\n"
                  "date: Mon, 21 Oct 2013 20:13:22 GMT\n"
                  "location: https://www.example.com\n"
                  "content-encoding: gzip\n"
                  "set-cookie: foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; "
                  "version=1\n",
                  0},
             }},
        Test{"IllegalHpackTableGrowth",
             {},
             {1024},
             {{"3fc43fc4", absl::InternalError("Attempt to make hpack table"),
               kFailureIsConnectionError}}},
        Test{"FuzzerFoundInvalidHpackIndexFuzzerFound1",
             {},
             {},
             {{"3ba4a41007f0a40f2d62696e8b632a5b29a40fa4a4281007f0",
               absl::InternalError("Invalid HPACK index received"),
               kFailureIsConnectionError}}},
        Test{"FuzzerFoundMultipleTableSizeChanges1",
             {},
             {},
             {{"2aa41007f0a40f2d62696e8163a41f1f00275bf0692862a4dbf0f00963",
               absl::InternalError(
                   "More than two max table size changes in a single frame"),
               kFailureIsConnectionError}}},
        Test{"FuzzerFoundIllegalHeaderKey1",
             {},
             {},
             {{"2aa41007f0a40f2d62696e8363271f00275bf06928626e2d213fa40fdbf0212"
               "8215cf00963",
               absl::InternalError("Illegal header key"), 0}}},
        Test{"FuzzerFoundMultipleTableSizeChanges2",
             {},
             {},
             {{"a4a41007f0a40f2d62696e8b635b29282d2762696e3b0921213fa41fdbf0211"
               "007f07b282d62696ef009215c0921e51fe91b3b3f47ed5b282821215cf0",
               absl::InternalError(
                   "More than two max table size changes in a single frame"),
               kFailureIsConnectionError}}},
        Test{
            "FuzzerFoundInterOverflow1",
            {},
            {},
            {{"696969696969696969696969696969696969696969696969696969696969696"
              "969696969696969696969696969696969696969696969696969696969696969"
              "6969696969696969696969696969bababababababababababababababababab"
              "abababababababababababababababababababababababababababababababa"
              "bababababababababababababababababababababababababababababababab"
              "abababababaa4a41007f0a40f2d62696e8bffffffffffffffffffffffffffff"
              "ffffffffffff632a5b29a428a42d0fdbf027f0628363696e092121",
              absl::InternalError("integer overflow in hpack integer decoding"),
              kEndOfHeaders | kFailureIsConnectionError}}},
        Test{"StatusIsAnInteger",
             {},
             {},
             {{"0e 00 00 df",
               absl::InternalError("Error parsing ':status' metadata"), 0}}},
        Test{"BinaryMetadataFromBase64",
             {},
             {},
             {
                 // Binary metadata: created using:
                 // tools/codegen/core/gen_header_frame.py
                 //    --compression inc --no_framing --output hexstr
                 //    < test/core/transport/chttp2/binary-metadata.headers
                 {"40 09 61 2e 62 2e 63 2d 62 69 6e 0c 62 32 31 6e 4d 6a 41 79 "
                  "4d 51 3d 3d",
                  "a.b.c-bin: omg2021\n", 0},
             }},
        Test{"Base64LegalEncoding",
             {},
             {},
             {// Binary metadata: created using:
              // tools/codegen/core/gen_header_frame.py
              //    --compression inc --no_framing --output hexstr
              //    < test/core/transport/chttp2/bad-base64.headers
              {"4009612e622e632d62696e1c6c75636b696c7920666f722075732c206974"
               "27732074756573646179",
               absl::InternalError("Error parsing 'a.b.c-bin' metadata: "
                                   "illegal base64 encoding"),
               0},
              {"be",
               absl::InternalError("Error parsing 'a.b.c-bin' metadata: "
                                   "illegal base64 encoding"),
               0}}},
        Test{"TeIsTrailers",
             {},
             {},
             {// created using:
              // tools/codegen/core/gen_header_frame.py
              //    --compression inc --no_framing --output hexstr
              //    < test/core/transport/chttp2/bad-te.headers
              {"400274650767617262616765",
               absl::InternalError("Error parsing 'te' metadata"), 0},
              {"be", absl::InternalError("Error parsing 'te' metadata"), 0}}},
        Test{"MetadataSizeLimitCheck",
             {},
             128,
             {
                 {// Generated with: tools/codegen/core/gen_header_frame.py
                  // --compression inc --output hexstr --no_framing <
                  // test/core/transport/chttp2/large-metadata.headers
                  "40096164616c64726964610a6272616e64796275636b40086164616c6772"
                  "696d04746f6f6b4008616d6172616e74680a6272616e64796275636b4008"
                  "616e67656c6963610762616767696e73",
                  absl::ResourceExhaustedError(
                      "received metadata size exceeds hard limit"),
                  kEndOfHeaders},
                 // Should be able to look up the added elements individually
                 // (do not corrupt the hpack table test!)
                 {"be", "angelica: baggins\n", kEndOfHeaders},
                 {"bf", "amaranth: brandybuck\n", kEndOfHeaders},
                 {"c0", "adalgrim: took\n", kEndOfHeaders},
                 {"c1", "adaldrida: brandybuck\n", kEndOfHeaders},
                 // But not as a whole - that exceeds metadata limits for one
                 // request again
                 {"bebfc0c1",
                  absl::ResourceExhaustedError(
                      "received metadata size exceeds hard limit"),
                  0},
             }},
        Test{
            "SingleByteBE",
            {},
            {},
            {{"be", absl::InternalError("Invalid HPACK index received"),
              kFailureIsConnectionError}},
        },
        Test{
            "SingleByte80",
            {},
            {},
            {{"80", absl::InternalError("Illegal hpack op code"),
              kFailureIsConnectionError}},
        },
        Test{
            "SingleByte29",
            {},
            {},
            {{"29", "", kFailureIsConnectionError}},
        },
        Test{
            "EmptyWithPriority",
            {},
            {},
            {{"", "", kWithPriority}},
        },
        Test{
            "SingleByteF5",
            {},
            {},
            {{"f5", absl::InternalError("Invalid HPACK index received"),
              kFailureIsConnectionError}},
        },
        Test{
            "SingleByte0f",
            {},
            {},
            {{"0f", "", 0}},
        },
        Test{
            "SingleByte7f",
            {},
            {},
            {{"7f", "", 0}},
        },
        Test{
            "FuzzerCoverage1bffffff7c1b",
            {},
            {},
            {{"1bffffff7c1b",
              absl::ResourceExhaustedError(
                  "received metadata size exceeds hard limit"),
              0}},
        },
        Test{
            "FuzzerCoverageffffffffff00ff",
            {},
            {},
            {{"ffffffffff00ff",
              absl::InternalError("Invalid HPACK index received"),
              kFailureIsConnectionError}},
        },
        Test{
            "FuzzerCoverageIntegerOverflow2",
            {},
            {},
            {{"ff8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8"
              "d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d"
              "8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8d8"
              "d8d8d8d8d8d8d8d",
              absl::InternalError("integer overflow in hpack integer decoding"),
              kFailureIsConnectionError}}},
        Test{
            "FuzzerCoverageMetadataLimits",
            {},
            {9},
            {{"3f6672616d6573207ba2020656e645f6f665f686561646572733a2074727565a"
              "2020656e645f6f665f73747265616d3a2074727565a202073746f705f6275666"
              "66572696e675f61667465725f7365676d656e74733a2039a202070617273653a"
              "20225c3030305c3030305c3030305c3030305c3030305c3030305c3030305c30"
              "30305c3030305c3030305c3030305c3030305c3030305c3030305c3030305c30"
              "30305c3030305c3030305c3030305c3030305c3030305c3030305c3030305c",
              absl::ResourceExhaustedError(
                  "received metadata size exceeds hard limit"),
              kWithPriority}}},
        Test{"FuzzerCoverage52046772706300073a737461747573033230300e7f",
             {},
             {},
             {{"52046772706300073a737461747573033230300e7f",
               ":status: 200\naccept-ranges: grpc\n", 0}}},
        Test{"FuzzerCoveragea4a41007f0a40f2d62696e8beda42d5b63272129a410626907",
             {},
             {},
             {{"a4a41007f0a40f2d62696e8beda42d5b63272129a410626907",
               absl::InternalError("Illegal header key"), 0}}},
        Test{
            "HpackTableSizeWithBase64",
            // haiku segment: 149bytes*2, a:a segment: 34 bytes
            // So we arrange for one less than the total so we force a hpack
            // table overflow
            {149 * 2 + 34 - 1},
            {},
            {
                {// Generated with: tools/codegen/core/gen_header_frame.py
                 // --compression inc --output hexstr --no_framing <
                 // test/core/transport/chttp2/long-base64.headers
                 "4005782d62696e70516d467a5a545930494756755932396b6157356e4f67"
                 "704a644342305957746c6379426961573568636e6b675a47463059534268"
                 "626d5167625746725a584d6761585167644756346443344b56584e6c5a6e5"
                 "67349475a766369427a644739796157356e49475a706247567a4c673d3d",
                 // Haiku by Bard.
                 "x-bin: Base64 encoding:\nIt takes binary data and makes it "
                 "text.\nUseful for storing files.\n",
                 0},
                // Should go into the hpack table (x-bin: ... is 149 bytes long
                // by hpack rules)
                {"be",
                 "x-bin: Base64 encoding:\nIt takes binary data and "
                 "makes it text.\nUseful for storing files.\n",
                 0},
                // Add another copy
                {"4005782d62696e70516d467a5a545930494756755932396b6157356e4f67"
                 "704a644342305957746c6379426961573568636e6b675a47463059534268"
                 "626d5167625746725a584d6761585167644756346443344b56584e6c5a6e5"
                 "67349475a766369427a644739796157356e49475a706247567a4c673d3d",
                 "x-bin: Base64 encoding:\nIt takes binary data and makes it "
                 "text.\nUseful for storing files.\n",
                 0},
                // 149*2 == 298, so we should have two copies in the hpack table
                {"bebf",
                 "x-bin: Base64 encoding:\nIt takes binary data and makes it "
                 "text.\nUseful for storing files.\n"
                 "x-bin: Base64 encoding:\nIt takes binary data and makes it "
                 "text.\nUseful for storing files.\n",
                 0},
                // Add some very short headers (should push the first long thing
                // out)
                // Generated with: tools/codegen/core/gen_header_frame.py
                // --compression inc --output hexstr --no_framing <
                // test/core/transport/chttp2/short.headers
                {"4001610161", "a: a\n", 0},
                // First two entries should be what was just pushed and then one
                // long entry
                {"bebf",
                 "a: a\nx-bin: Base64 encoding:\nIt takes binary data and "
                 "makes "
                 "it text.\nUseful for storing files.\n",
                 0},
                // Third entry should be unprobable (it's no longer in the
                // table!)
                {"c0", absl::InternalError("Invalid HPACK index received"),
                 kFailureIsConnectionError},
            }},
        Test{
            "HpackTableSizeWithBase64AndHuffman",
            // haiku segment: 149bytes*2, a:a segment: 34 bytes
            // So we arrange for one less than the total so we force a hpack
            // table overflow
            {149 * 2 + 34 - 1},
            {},
            {
                {// Generated with: tools/codegen/core/gen_header_frame.py
                 // --compression inc --output hexstr --no_framing --huff <
                 // test/core/transport/chttp2/long-base64.headers
                 "4005782d62696edbd94e1f7fbbf983262e36f313fd47c9bab54d5e592f5d0"
                 "73e49a09eae987c9b9c95759bf7161073dd7678e9d9347cb0d9fbf9a261fe"
                 "6c9a4c5c5a92f359b8fe69a3f6ae28c98bf7b90d77dc989ff43e4dd59317e"
                 "d71e2e3ef3cd041",
                 // Haiku by Bard.
                 "x-bin: Base64 encoding:\nIt takes binary data and makes it "
                 "text.\nUseful for storing files.\n",
                 0},
                // Should go into the hpack table (x-bin: ... is 149 bytes long
                // by hpack rules)
                {"be",
                 "x-bin: Base64 encoding:\nIt takes binary data and "
                 "makes it text.\nUseful for storing files.\n",
                 0},
                // Add another copy
                {"4005782d62696edbd94e1f7fbbf983262e36f313fd47c9bab54d5e592f5d0"
                 "73e49a09eae987c9b9c95759bf7161073dd7678e9d9347cb0d9fbf9a261fe"
                 "6c9a4c5c5a92f359b8fe69a3f6ae28c98bf7b90d77dc989ff43e4dd59317e"
                 "d71e2e3ef3cd041",
                 "x-bin: Base64 encoding:\nIt takes binary data and makes it "
                 "text.\nUseful for storing files.\n",
                 0},
                // 149*2 == 298, so we should have two copies in the hpack table
                {"bebf",
                 "x-bin: Base64 encoding:\nIt takes binary data and makes it "
                 "text.\nUseful for storing files.\n"
                 "x-bin: Base64 encoding:\nIt takes binary data and makes it "
                 "text.\nUseful for storing files.\n",
                 0},
                // Add some very short headers (should push the first long thing
                // out)
                // Generated with: tools/codegen/core/gen_header_frame.py
                // --compression inc --output hexstr --no_framing <
                // test/core/transport/chttp2/short.headers
                {"4001610161", "a: a\n", 0},
                // First two entries should be what was just pushed and then one
                // long entry
                {"bebf",
                 "a: a\nx-bin: Base64 encoding:\nIt takes binary data and "
                 "makes "
                 "it text.\nUseful for storing files.\n",
                 0},
                // Third entry should be unprobable (it's no longer in the
                // table!)
                {"c0", absl::InternalError("Invalid HPACK index received"),
                 kFailureIsConnectionError},
            }},
        Test{"SingleByte7a", {}, {}, {{"7a", "", 0}}},
        Test{"SingleByte60",
             {},
             {},
             {{"60",
               absl::InternalError("Incomplete header at the end of a "
                                   "header/continuation sequence"),
               kEndOfStream | kFailureIsConnectionError}}},
        Test{"FuzzerFoundMultipleTableSizeChanges3",
             {},
             {},
             {{"89", ":status: 204\n", 0},
              {"89", ":status: 204\n", 0},
              {"393939393939393939393939393939393939393939",
               absl::InternalError(
                   "More than two max table size changes in a single frame"),
               kFailureIsConnectionError}}},
        Test{"FuzzerCoverage4005782d62696edbd94e1f7etc",
             {},
             {},
             {{"4005782d62696edbd94e1f7fbbf983267e36a313fd47c9bab54d5e592f5d",
               "", 0}}},
        Test{"FuzzerCoverage72656672657368",
             {},
             {},
             {{"72656672657368", "", 0}}},
        Test{"FuzzerCoverage66e6645f74Then66645f74",
             {},
             {},
             {{"66e6645f74", "", 0}, {"66645f74", "", 0}}},
        Test{
            "MixedCaseHeadersAreStreamErrors",
            {},
            {},
            {{// Generated with: tools/codegen/core/gen_header_frame.py
              // --compression inc --output hexstr --no_framing <
              // test/core/transport/chttp2/MiXeD-CaSe.headers
              "400a4d695865442d436153651073686f756c64206e6f74207061727365",
              absl::InternalError("Illegal header key: MiXeD-CaSe"), 0},
             {// Looking up with hpack indices should work, but also return
              // error
              "be", absl::InternalError("Illegal header key: MiXeD-CaSe"), 0}}},
        Test{
            "FuzzerCoverageIntegerOverflow3",
            {},
            {},
            {{"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
              absl::InternalError("integer overflow in hpack integer decoding"),
              kFailureIsConnectionError}}},
        Test{"Dadadadadada",
             {},
             {},
             {{"dadadadadadadadadadadadadadadadadadadadadadadadadadadadadadadad"
               "adadadadadadadadadadadadadadadadadadadadadadadadadadadadadadada"
               "dadadadadadadadadadadadadadadadadadadadadadadadadadadadadadadad"
               "adadadadadadadadadadadadadadadadadadada",
               absl::InternalError("Invalid HPACK index received"),
               kWithPriority | kFailureIsConnectionError}}},
        Test{"MaliciousVarintEncoding",
             {},
             {},
             {{"1f80808080808080808080808080808080808080808080808080808080",
               absl::InternalError(
                   "Malicious varint encoding detected in HPACK stream"),
               kFailureIsConnectionError}}}),
    NameFromConfig);

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
