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

#include <google/protobuf/text_format.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/random/bit_gen_ref.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder_table.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser_table.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "test/core/test_util/fuzz_config_vars.h"
#include "test/core/test_util/fuzz_config_vars_helpers.h"
#include "test/core/test_util/proto_bit_gen.h"
#include "test/core/test_util/test_config.h"
#include "test/core/transport/chttp2/hpack_sync_fuzzer.pb.h"

namespace grpc_core {
namespace {

bool IsStreamError(const absl::Status& status) {
  intptr_t stream_id;
  return grpc_error_get_int(status, StatusIntProperty::kStreamId, &stream_id);
}

void FuzzOneInput(const hpack_sync_fuzzer::Msg& msg) {
  ApplyFuzzConfigVars(msg.config_vars());
  TestOnlyReloadExperimentsFromConfigVariables();
  ProtoBitGen proto_bit_src(msg.random_numbers());

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
        if (msg.check_ab_preservation() &&
            header.literal_inc_idx().key() == "a") {
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
        if (msg.check_ab_preservation() &&
            header.literal_not_idx().key() == "a") {
          continue;
        }
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
  if (msg.check_ab_preservation()) {
    std::ignore = encoder.EmitLitHdrWithNonBinaryStringKeyIncIdx(
        Slice::FromCopiedString("a"), Slice::FromCopiedString("b"));
  }

  // STAGE 2: Decode the buffer (encode_output) into a list of headers
  HPackParser parser;
  ExecCtx exec_ctx;
  grpc_metadata_batch read_metadata;
  parser.BeginFrame(
      &read_metadata, 1024, 1024, HPackParser::Boundary::EndOfHeaders,
      HPackParser::Priority::None,
      HPackParser::LogInfo{1, HPackParser::LogInfo::kHeaders, false});
  std::vector<std::pair<size_t, absl::Status>> seen_errors;
  for (size_t i = 0; i < encode_output.Count(); i++) {
    auto err = parser.Parse(
        encode_output.c_slice_at(i), i == (encode_output.Count() - 1),
        absl::BitGenRef(proto_bit_src), /*call_tracer=*/nullptr);
    if (!err.ok()) {
      seen_errors.push_back(std::pair(i, err));
      // If we get a connection error (i.e. not a stream error), stop parsing,
      // return.
      if (!IsStreamError(err)) return;
    }
  }

  if (seen_errors.empty() && msg.check_ab_preservation()) {
    std::string backing;
    auto a_value = read_metadata.GetStringValue("a", &backing);
    if (!a_value.has_value()) {
      fprintf(stderr, "Expected 'a' header to be present: %s\n",
              read_metadata.DebugString().c_str());
      abort();
    }
    if (a_value != "b") {
      fprintf(stderr, "Expected 'a' header to be 'b', got '%s'\n",
              std::string(*a_value).c_str());
      abort();
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

  if (msg.check_ab_preservation()) {
    SliceBuffer encode_output_2;
    hpack_encoder_detail::Encoder encoder_2(
        &compressor, msg.use_true_binary_metadata(), encode_output_2);
    encoder_2.EmitIndexed(62);
    CHECK_EQ(encode_output_2.Count(), 1);
    grpc_metadata_batch read_metadata_2;
    parser.BeginFrame(
        &read_metadata_2, 1024, 1024, HPackParser::Boundary::EndOfHeaders,
        HPackParser::Priority::None,
        HPackParser::LogInfo{3, HPackParser::LogInfo::kHeaders, false});
    auto err = parser.Parse(encode_output_2.c_slice_at(0), true,
                            absl::BitGenRef(proto_bit_src),
                            /*call_tracer=*/nullptr);
    if (!err.ok()) {
      fprintf(stderr, "Error parsing preservation encoded data: %s\n",
              err.ToString().c_str());
      abort();
    }
    std::string backing;
    auto a_value = read_metadata_2.GetStringValue("a", &backing);
    if (!a_value.has_value()) {
      fprintf(stderr,
              "Expected 'a' header to be present: %s\nfirst metadata: %s\n",
              read_metadata_2.DebugString().c_str(),
              read_metadata.DebugString().c_str());
      abort();
    }
    if (a_value != "b") {
      fprintf(stderr, "Expected 'a' header to be 'b', got '%s'\n",
              std::string(*a_value).c_str());
      abort();
    }
  }
}
FUZZ_TEST(HpackSyncFuzzer, FuzzOneInput)
    .WithDomains(::fuzztest::Arbitrary<hpack_sync_fuzzer::Msg>()
                     .WithProtobufField("config_vars", AnyConfigVars()));

auto ParseTestProto(const std::string& proto) {
  hpack_sync_fuzzer::Msg msg;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &msg));
  return msg;
}

TEST(HpackSyncFuzzer, FuzzOneInputRegression1) {
  FuzzOneInput(ParseTestProto(
      R"pb(
        headers { literal_not_idx { key: "grpc-status" value: "72" } }
      )pb"));
}

TEST(HpackSyncFuzzer, FuzzOneInputRegression2) {
  FuzzOneInput(ParseTestProto(
      R"pb(
        headers { literal_not_idx { key: "grpc-status" value: "-1" } }
      )pb"));
}

}  // namespace
}  // namespace grpc_core
