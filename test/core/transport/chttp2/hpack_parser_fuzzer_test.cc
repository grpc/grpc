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

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "absl/cleanup/cleanup.h"

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/transport/chttp2/hpack_parser_fuzzer.pb.h"

// IWYU pragma: no_include <google/protobuf/repeated_ptr_field.h>

bool squelch = true;
bool leak_check = true;

static void dont_log(gpr_log_func_args* /*args*/) {}

DEFINE_PROTO_FUZZER(const hpack_parser_fuzzer::Msg& msg) {
  if (squelch) gpr_set_log_function(dont_log);
  grpc_init();
  auto cleanup = absl::MakeCleanup(grpc_shutdown);
  auto memory_allocator = grpc_core::ResourceQuota::Default()
                              ->memory_quota()
                              ->CreateMemoryAllocator("test-allocator");
  {
    std::unique_ptr<grpc_core::HPackParser> parser(new grpc_core::HPackParser);
    int max_length = 1024;
    int absolute_max_length = 1024;
    bool can_update_max_length = true;
    bool can_add_priority = true;
    for (int i = 0; i < msg.frames_size(); i++) {
      auto arena = grpc_core::MakeScopedArena(1024, &memory_allocator);
      grpc_core::ExecCtx exec_ctx;
      grpc_metadata_batch b(arena.get());
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
      grpc_core::HPackParser::Priority priority =
          grpc_core::HPackParser::Priority::None;
      if (can_add_priority && frame.priority()) {
        priority = grpc_core::HPackParser::Priority::Included;
      }
      grpc_core::HPackParser::Boundary boundary =
          grpc_core::HPackParser::Boundary::None;
      can_update_max_length = false;
      can_add_priority = false;
      if (frame.end_of_headers()) {
        boundary = grpc_core::HPackParser::Boundary::EndOfHeaders;
        can_update_max_length = true;
      }
      if (frame.end_of_stream()) {
        boundary = grpc_core::HPackParser::Boundary::EndOfStream;
        can_update_max_length = true;
        can_add_priority = true;
      }

      parser->BeginFrame(
          &b, max_length, absolute_max_length, boundary, priority,
          grpc_core::HPackParser::LogInfo{
              1, grpc_core::HPackParser::LogInfo::kHeaders, false});
      int stop_buffering_ctr =
          std::max(-1, frame.stop_buffering_after_segments());
      for (int idx = 0; idx < frame.parse_size(); idx++) {
        const auto& parse = frame.parse(idx);
        grpc_slice buffer =
            grpc_slice_from_copied_buffer(parse.data(), parse.size());
        auto err = parser->Parse(buffer, idx == frame.parse_size() - 1);
        grpc_slice_unref(buffer);
        stop_buffering_ctr--;
        if (0 == stop_buffering_ctr) parser->StopBufferingFrame();
        // Ensure we never take on more than four times the absolute limit in
        // buffer size.
        // (This is incredibly generous, but having a bound nevertheless means
        // we don't accidentally flow to infinity, which would be crossing the
        // streams level bad).
        GPR_ASSERT(static_cast<int>(parser->buffered_bytes() / 4) <
                   std::max(1024, absolute_max_length));
        if (!err.ok()) {
          intptr_t unused;
          if (grpc_error_get_int(err, grpc_core::StatusIntProperty::kStreamId,
                                 &unused)) {
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
