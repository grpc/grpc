// Copyright 2023 gRPC authors.
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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"

#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder_table.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser_table.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/transport/chttp2/hpack_sync_fuzzer.pb.h"
#include "test/core/util/fuzz_config_vars.h"

bool squelch = true;
bool leak_check = true;

static void dont_log(gpr_log_func_args* /*args*/) {}

namespace grpc_core {
namespace {

bool IsStreamError(const absl::Status& status) {
  intptr_t stream_id;
  return grpc_error_get_int(status, StatusIntProperty::kStreamId, &stream_id);
}

void FuzzOneInput(const hpack_sync_fuzzer::Msg& msg) {
  // STAGE 1: Encode the fuzzing input into a buffer (encode_output)
  HPackCompressor compressor;
  SliceBuffer encode_output;
  hpack_encoder_detail::Encoder encoder(
      &compressor, msg.use_true_binary_metadata(), encode_output);
  for (const auto& header : msg.headers()) {
    switch (header.ty_case()) {
      case hpack_sync_fuzzer::Header::TY_NOT_SET:
        break;
      case hpack_sync_fuzzer::Header::kIndexed:
        if (header.indexed() == 0) continue;  // invalid encoding
        encoder.EmitIndexed(header.indexed());
        break;
      case hpack_sync_fuzzer::Header::kLiteralIncIdx:
        if (header.literal_inc_idx().key().length() +
                header.literal_inc_idx().value().length() >
            HPackEncoderTable::MaxEntrySize() / 2) {
          // Not an interesting case to fuzz
          continue;
        }
        if (absl::EndsWith(header.literal_inc_idx().value(), "-bin")) {
          std::ignore = encoder.EmitLitHdrWithBinaryStringKeyIncIdx(
              Slice::FromCopiedString(header.literal_inc_idx().key()),
              Slice::FromCopiedString(header.literal_inc_idx().value()));
        } else {
          std::ignore = encoder.EmitLitHdrWithNonBinaryStringKeyIncIdx(
              Slice::FromCopiedString(header.literal_inc_idx().key()),
              Slice::FromCopiedString(header.literal_inc_idx().value()));
        }
        break;
      case hpack_sync_fuzzer::Header::kLiteralNotIdx:
        if (absl::EndsWith(header.literal_not_idx().value(), "-bin")) {
          encoder.EmitLitHdrWithBinaryStringKeyNotIdx(
              Slice::FromCopiedString(header.literal_not_idx().key()),
              Slice::FromCopiedString(header.literal_not_idx().value()));
        } else {
          encoder.EmitLitHdrWithNonBinaryStringKeyNotIdx(
              Slice::FromCopiedString(header.literal_not_idx().key()),
              Slice::FromCopiedString(header.literal_not_idx().value()));
        }
        break;
      case hpack_sync_fuzzer::Header::kLiteralNotIdxFromIdx:
        if (header.literal_not_idx_from_idx().index() == 0) continue;
        encoder.EmitLitHdrWithBinaryStringKeyNotIdx(
            header.literal_not_idx_from_idx().index(),
            Slice::FromCopiedString(header.literal_not_idx_from_idx().value()));
        break;
    }
  }

  // STAGE 2: Decode the buffer (encode_output) into a list of headers
  HPackParser parser;
  auto memory_allocator =
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
          "test-allocator");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  ExecCtx exec_ctx;
  grpc_metadata_batch read_metadata(arena.get());
  parser.BeginFrame(
      &read_metadata, 1024, 1024, HPackParser::Boundary::EndOfHeaders,
      HPackParser::Priority::None,
      HPackParser::LogInfo{1, HPackParser::LogInfo::kHeaders, false});
  std::vector<std::pair<size_t, absl::Status>> seen_errors;
  for (size_t i = 0; i < encode_output.Count(); i++) {
    auto err = parser.Parse(encode_output.c_slice_at(i),
                            i == (encode_output.Count() - 1));
    if (!err.ok()) {
      seen_errors.push_back(std::make_pair(i, err));
      // If we get a connection error (i.e. not a stream error), stop parsing,
      // return.
      if (!IsStreamError(err)) return;
    }
  }

  // STAGE 3: If we reached here we either had a stream error or no error
  // parsing.
  // Either way, the hpack tables should be of the same size between client and
  // server.
  const auto encoder_size = encoder.hpack_table().test_only_table_size();
  const auto parser_size = parser.hpack_table()->test_only_table_size();
  const auto encoder_elems = encoder.hpack_table().test_only_table_elems();
  const auto parser_elems = parser.hpack_table()->num_entries();
  if (encoder_size != parser_size || encoder_elems != parser_elems) {
    fprintf(stderr, "Encoder size: %d Parser size: %d\n", encoder_size,
            parser_size);
    fprintf(stderr, "Encoder elems: %d Parser elems: %d\n", encoder_elems,
            parser_elems);
    if (!seen_errors.empty()) {
      fprintf(stderr, "Seen errors during parse:\n");
      for (const auto& error : seen_errors) {
        fprintf(stderr, "  [slice %" PRIdPTR "] %s\n", error.first,
                error.second.ToString().c_str());
      }
    }
    fprintf(stderr, "Encoded data:\n");
    for (size_t i = 0; i < encode_output.Count(); i++) {
      fprintf(
          stderr, "  [slice %" PRIdPTR "]: %s\n", i,
          absl::BytesToHexString(encode_output[i].as_string_view()).c_str());
    }
    abort();
  }
}

}  // namespace
}  // namespace grpc_core

DEFINE_PROTO_FUZZER(const hpack_sync_fuzzer::Msg& msg) {
  if (squelch) gpr_set_log_function(dont_log);
  grpc_core::ApplyFuzzConfigVars(msg.config_vars());
  grpc_core::TestOnlyReloadExperimentsFromConfigVariables();
  grpc_core::FuzzOneInput(msg);
}
