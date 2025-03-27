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

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/random/bit_gen_ref.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "test/core/test_util/fuzz_config_vars.h"
#include "test/core/test_util/fuzz_config_vars_helpers.h"
#include "test/core/test_util/proto_bit_gen.h"
#include "test/core/test_util/test_config.h"
#include "test/core/transport/chttp2/hpack_parser_fuzzer.pb.h"

// IWYU pragma: no_include <google/protobuf/repeated_ptr_field.h>

namespace grpc_core {
void HpackParserFuzzer(const hpack_parser_fuzzer::Msg& msg) {
  ProtoBitGen proto_bit_src(msg.random_numbers());
  ApplyFuzzConfigVars(msg.config_vars());
  TestOnlyReloadExperimentsFromConfigVariables();
  grpc_init();
  auto cleanup = absl::MakeCleanup(grpc_shutdown);
  auto memory_allocator =
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
          "test-allocator");
  {
    std::unique_ptr<HPackParser> parser(new HPackParser);
    int max_length = 1024;
    int absolute_max_length = 1024;
    bool can_update_max_length = true;
    bool can_add_priority = true;
    for (int i = 0; i < msg.frames_size(); i++) {
      auto arena = SimpleArenaAllocator()->MakeArena();
      ExecCtx exec_ctx;
      grpc_metadata_batch b;
      const auto& frame = msg.frames(i);
      if (frame.parse_size() == 0) continue;

      // we can only update max length after a frame boundary
      // so simulate that here
      if (can_update_max_length) {
        if (frame.max_metadata_length() != 0) {
          max_length = std::max(0, frame.max_metadata_length());
        }
        if (frame.absolute_max_metadata_length() != 0) {
          absolute_max_length =
              std::max(0, frame.absolute_max_metadata_length());
        }
        if (absolute_max_length < max_length) {
          std::swap(absolute_max_length, max_length);
        }
      }
      // priority only makes sense on the first frame of a stream
      HPackParser::Priority priority = HPackParser::Priority::None;
      if (can_add_priority && frame.priority()) {
        priority = HPackParser::Priority::Included;
      }
      HPackParser::Boundary boundary = HPackParser::Boundary::None;
      can_update_max_length = false;
      can_add_priority = false;
      if (frame.end_of_headers()) {
        boundary = HPackParser::Boundary::EndOfHeaders;
        can_update_max_length = true;
      }
      if (frame.end_of_stream()) {
        boundary = HPackParser::Boundary::EndOfStream;
        can_update_max_length = true;
        can_add_priority = true;
      }

      parser->BeginFrame(
          &b, max_length, absolute_max_length, boundary, priority,
          HPackParser::LogInfo{1, HPackParser::LogInfo::kHeaders, false});
      int stop_buffering_ctr =
          std::max(-1, frame.stop_buffering_after_segments());
      for (int idx = 0; idx < frame.parse_size(); idx++) {
        const auto& parse = frame.parse(idx);
        grpc_slice buffer =
            grpc_slice_from_copied_buffer(parse.data(), parse.size());
        auto err = parser->Parse(buffer, idx == frame.parse_size() - 1,
                                 absl::BitGenRef(proto_bit_src),
                                 /*call_tracer=*/nullptr);
        grpc_slice_unref(buffer);
        stop_buffering_ctr--;
        if (0 == stop_buffering_ctr) parser->StopBufferingFrame();
        // Ensure we never take on more than four times the absolute limit in
        // buffer size.
        // (This is incredibly generous, but having a bound nevertheless means
        // we don't accidentally flow to infinity, which would be crossing the
        // streams level bad).
        CHECK(static_cast<int>(parser->buffered_bytes() / 4) <
              std::max(1024, absolute_max_length));
        if (!err.ok()) {
          intptr_t unused;
          if (grpc_error_get_int(err, StatusIntProperty::kStreamId, &unused)) {
            // This is a stream error, we ignore it
          } else {
            // This is a connection error, we don't try to parse anymore
            return;
          }
        }
      }
      parser->FinishFrame();
    }
  }
}
FUZZ_TEST(HpackParser, HpackParserFuzzer)
    .WithDomains(::fuzztest::Arbitrary<hpack_parser_fuzzer::Msg>()
                     .WithProtobufField("config_vars", AnyConfigVars()));

}  // namespace grpc_core
