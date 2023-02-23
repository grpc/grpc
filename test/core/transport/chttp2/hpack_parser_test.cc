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

#include <stdlib.h>

#include <initializer_list>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "test/core/util/parse_hexstring.h"
#include "test/core/util/slice_splitter.h"
#include "test/core/util/test_config.h"

struct TestInput {
  const char* input;
  const char* expected_parse;
};

struct Test {
  absl::optional<size_t> table_size;
  std::vector<TestInput> inputs;
};

class ParseTest : public ::testing::TestWithParam<Test> {
 public:
  ParseTest() {
    grpc_init();
    parser_ = std::make_unique<grpc_core::HPackParser>();
  }

  ~ParseTest() override {
    {
      grpc_core::ExecCtx exec_ctx;
      parser_.reset();
    }

    grpc_shutdown();
  }

  void SetUp() override {
    if (GetParam().table_size.has_value()) {
      parser_->hpack_table()->SetMaxBytes(GetParam().table_size.value());
      EXPECT_EQ(parser_->hpack_table()->SetCurrentTableSize(
                    GetParam().table_size.value()),
                absl::OkStatus());
    }
  }

  void TestVector(grpc_slice_split_mode mode, const char* hexstring,
                  std::string expect) {
    grpc_core::MemoryAllocator memory_allocator =
        grpc_core::MemoryAllocator(grpc_core::ResourceQuota::Default()
                                       ->memory_quota()
                                       ->CreateMemoryAllocator("test"));
    auto arena = grpc_core::MakeScopedArena(1024, &memory_allocator);
    grpc_core::ExecCtx exec_ctx;
    grpc_slice input = parse_hexstring(hexstring);
    grpc_slice* slices;
    size_t nslices;
    size_t i;

    grpc_metadata_batch b(arena.get());

    parser_->BeginFrame(
        &b, 4096, grpc_core::HPackParser::Boundary::None,
        grpc_core::HPackParser::Priority::None,
        grpc_core::HPackParser::LogInfo{
            1, grpc_core::HPackParser::LogInfo::kHeaders, false});

    grpc_split_slices(mode, &input, 1, &slices, &nslices);
    grpc_slice_unref(input);

    for (i = 0; i < nslices; i++) {
      grpc_core::ExecCtx exec_ctx;
      auto err = parser_->Parse(slices[i], i == nslices - 1);
      if (!err.ok()) {
        grpc_core::Crash(
            absl::StrFormat("Unexpected parse error: %s",
                            grpc_core::StatusToString(err).c_str()));
      }
    }

    for (i = 0; i < nslices; i++) {
      grpc_slice_unref(slices[i]);
    }
    gpr_free(slices);

    TestEncoder encoder;
    b.Encode(&encoder);
    EXPECT_EQ(encoder.result(), expect);
  }

 private:
  class TestEncoder {
   public:
    std::string result() { return out_; }

    void Encode(const grpc_core::Slice& key, const grpc_core::Slice& value) {
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

  std::unique_ptr<grpc_core::HPackParser> parser_;
};

TEST_P(ParseTest, WholeSlices) {
  for (const auto& input : GetParam().inputs) {
    TestVector(GRPC_SLICE_SPLIT_MERGE_ALL, input.input, input.expected_parse);
  }
}

TEST_P(ParseTest, OneByteAtATime) {
  for (const auto& input : GetParam().inputs) {
    TestVector(GRPC_SLICE_SPLIT_ONE_BYTE, input.input, input.expected_parse);
  }
}

INSTANTIATE_TEST_SUITE_P(
    ParseTest, ParseTest,
    ::testing::Values(
        Test{
            {},
            {
                // D.2.1
                {"400a 6375 7374 6f6d 2d6b 6579 0d63 7573"
                 "746f 6d2d 6865 6164 6572",
                 "custom-key: custom-header\n"},
                // D.2.2
                {"040c 2f73 616d 706c 652f 7061 7468", ":path: /sample/path\n"},
                // D.2.3
                {"1008 7061 7373 776f 7264 0673 6563 7265"
                 "74",
                 "password: secret\n"},
                // D.2.4
                {"82", ":method: GET\n"},
            }},
        Test{{},
             {
                 // D.3.1
                 {"8286 8441 0f77 7777 2e65 7861 6d70 6c65"
                  "2e63 6f6d",
                  ":path: /\n"
                  ":authority: www.example.com\n"
                  ":method: GET\n"
                  ":scheme: http\n"},
                 // D.3.2
                 {"8286 84be 5808 6e6f 2d63 6163 6865",
                  ":path: /\n"
                  ":authority: www.example.com\n"
                  ":method: GET\n"
                  ":scheme: http\n"
                  "cache-control: no-cache\n"},
                 // D.3.3
                 {"8287 85bf 400a 6375 7374 6f6d 2d6b 6579"
                  "0c63 7573 746f 6d2d 7661 6c75 65",
                  ":path: /index.html\n"
                  ":authority: www.example.com\n"
                  ":method: GET\n"
                  ":scheme: https\n"
                  "custom-key: custom-value\n"},
             }},
        Test{{},
             {
                 // D.4.1
                 {"8286 8441 8cf1 e3c2 e5f2 3a6b a0ab 90f4"
                  "ff",
                  ":path: /\n"
                  ":authority: www.example.com\n"
                  ":method: GET\n"
                  ":scheme: http\n"},
                 // D.4.2
                 {"8286 84be 5886 a8eb 1064 9cbf",
                  ":path: /\n"
                  ":authority: www.example.com\n"
                  ":method: GET\n"
                  ":scheme: http\n"
                  "cache-control: no-cache\n"},
                 // D.4.3
                 {"8287 85bf 4088 25a8 49e9 5ba9 7d7f 8925"
                  "a849 e95b b8e8 b4bf",
                  ":path: /index.html\n"
                  ":authority: www.example.com\n"
                  ":method: GET\n"
                  ":scheme: https\n"
                  "custom-key: custom-value\n"},
             }},
        Test{{256},
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
                  "location: https://www.example.com\n"},
                 // D.5.2
                 {"4803 3330 37c1 c0bf",
                  ":status: 307\n"
                  "cache-control: private\n"
                  "date: Mon, 21 Oct 2013 20:13:21 GMT\n"
                  "location: https://www.example.com\n"},
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
                  "version=1\n"},
             }},
        Test{{256},
             {
                 // D.6.1
                 {"4882 6402 5885 aec3 771a 4b61 96d0 7abe"
                  "9410 54d4 44a8 2005 9504 0b81 66e0 82a6"
                  "2d1b ff6e 919d 29ad 1718 63c7 8f0b 97c8"
                  "e9ae 82ae 43d3",
                  ":status: 302\n"
                  "cache-control: private\n"
                  "date: Mon, 21 Oct 2013 20:13:21 GMT\n"
                  "location: https://www.example.com\n"},
                 // D.6.2
                 {"4883 640e ffc1 c0bf",
                  ":status: 307\n"
                  "cache-control: private\n"
                  "date: Mon, 21 Oct 2013 20:13:21 GMT\n"
                  "location: https://www.example.com\n"},
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
                  "version=1\n"},
             }},
        Test{{},
             {
                 // Binary metadata: created using:
                 // tools/codegen/core/gen_header_frame.py
                 //    --compression inc --no_framing --hex
                 //    < test/core/transport/chttp2/binary-metadata.headers
                 {"40 09 61 2e 62 2e 63 2d 62 69 6e 0c 62 32 31 6e 4d 6a 41 79 "
                  "4d 51 3d 3d",
                  "a.b.c-bin: omg2021\n"},
             }}));

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
