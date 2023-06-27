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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/hpack_parse_result.h"

#include <stddef.h>

#include <initializer_list>

#include "absl/strings/str_format.h"

#include "src/core/ext/transport/chttp2/transport/hpack_constants.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {

namespace {
class MetadataSizeLimitExceededEncoder {
 public:
  explicit MetadataSizeLimitExceededEncoder(std::string& summary)
      : summary_(summary) {}

  void Encode(const Slice& key, const Slice& value) {
    AddToSummary(key.as_string_view(), value.size());
  }

  template <typename Key, typename Value>
  void Encode(Key, const Value& value) {
    AddToSummary(Key::key(), EncodedSizeOfKey(Key(), value));
  }

 private:
  void AddToSummary(absl::string_view key,
                    size_t value_length) GPR_ATTRIBUTE_NOINLINE {
    absl::StrAppend(&summary_, " ", key, ":",
                    hpack_constants::SizeForEntry(key.size(), value_length),
                    "B");
  }
  std::string& summary_;
};

absl::Status MakeStreamError(absl::Status error) {
  GPR_DEBUG_ASSERT(!error.ok());
  return grpc_error_set_int(std::move(error), StatusIntProperty::kStreamId, 0);
}
}  // namespace

absl::Status HpackParseResult::Materialize() const {
  if (materialized_status_.has_value()) return *materialized_status_;
  materialized_status_ = BuildMaterialized();
  return *materialized_status_;
}

absl::Status HpackParseResult::BuildMaterialized() const {
  switch (status_.get()) {
    case HpackParseStatus::kOk:
      return absl::OkStatus();
    case HpackParseStatus::kEof:
      Crash("Materialize() called on EOF");
      break;
    case HpackParseStatus::kMovedFrom:
      Crash("Materialize() called on moved-from object");
      break;
    case HpackParseStatus::kInvalidMetadata:
      if (key_.empty()) {
        return MakeStreamError(absl::InternalError(
            ValidateMetadataResultToString(validate_metadata_result_)));
      } else {
        return MakeStreamError(absl::InternalError(absl::StrCat(
            ValidateMetadataResultToString(validate_metadata_result_), ": ",
            key_)));
      }
    case HpackParseStatus::kSoftMetadataLimitExceeded:
    case HpackParseStatus::kHardMetadataLimitExceeded: {
      const auto& e = metadata_limit_exceeded_;
      // Collect a summary of sizes so far for debugging
      // Do not collect contents, for fear of exposing PII.
      std::string summary;
      if (e.prior != nullptr) {
        MetadataSizeLimitExceededEncoder encoder(summary);
        e.prior->Encode(&encoder);
      }
      return MakeStreamError(absl::ResourceExhaustedError(absl::StrCat(
          "received metadata size exceeds ",
          status_.get() == HpackParseStatus::kSoftMetadataLimitExceeded
              ? "soft"
              : "hard",
          " limit (", e.frame_length, " vs. ", e.limit, ")",
          summary.empty() ? "" : "; ", summary)));
    }
    case HpackParseStatus::kHardMetadataLimitExceededByKey: {
      const auto& e = metadata_limit_exceeded_by_atom_;
      return MakeStreamError(absl::ResourceExhaustedError(
          absl::StrCat("received metadata size exceeds hard limit (key length ",
                       e.atom_length, " vs. ", e.limit, ")")));
    }
    case HpackParseStatus::kHardMetadataLimitExceededByValue: {
      const auto& e = metadata_limit_exceeded_by_atom_;
      return MakeStreamError(absl::ResourceExhaustedError(absl::StrCat(
          "received metadata size exceeds hard limit (value length ",
          e.atom_length, " vs. ", e.limit, ")")));
    }
    case HpackParseStatus::kMetadataParseError:
      if (!key_.empty()) {
        return MakeStreamError(absl::InternalError(
            absl::StrCat("Error parsing '", key_, "' metadata")));
      } else {
        return MakeStreamError(absl::InternalError("Error parsing metadata"));
      }
    case HpackParseStatus::kUnbase64Failed:
      if (!key_.empty()) {
        return MakeStreamError(absl::InternalError(absl::StrCat(
            "Error parsing '", key_, "' metadata: illegal base64 encoding")));
      } else {
        return MakeStreamError(absl::InternalError(
            absl::StrCat("Failed base64 decoding metadata")));
      }
    case HpackParseStatus::kIncompleteHeaderAtBoundary:
      return absl::InternalError(
          "Incomplete header at the end of a header/continuation sequence");
    case HpackParseStatus::kVarintOutOfRange:
      return absl::InternalError(absl::StrFormat(
          "integer overflow in hpack integer decoding: have 0x%08x, "
          "got byte 0x%02x",
          varint_out_of_range_.value, varint_out_of_range_.last_byte));
    case HpackParseStatus::kIllegalTableSizeChange:
      return absl::InternalError(absl::StrCat(
          "Attempt to make hpack table ", illegal_table_size_change_.new_size,
          " bytes when max is ", illegal_table_size_change_.max_size,
          " bytes"));
    case HpackParseStatus::kAddBeforeTableSizeUpdated:
      return absl::InternalError(
          absl::StrCat("HPACK max table size reduced to ",
                       illegal_table_size_change_.new_size,
                       " but not reflected by hpack stream (still at ",
                       illegal_table_size_change_.max_size, ")"));
    case HpackParseStatus::kParseHuffFailed:
      if (!key_.empty()) {
        return absl::InternalError(
            absl::StrCat("Failed huffman decoding '", key_, "' metadata"));
      } else {
        return absl::InternalError(
            absl::StrCat("Failed huffman decoding metadata"));
      }
      break;
    case HpackParseStatus::kTooManyDynamicTableSizeChanges:
      return absl::InternalError(
          "More than two max table size changes in a single frame");
    case HpackParseStatus::kMaliciousVarintEncoding:
      return absl::InternalError(
          "Malicious varint encoding detected in HPACK stream");
    case HpackParseStatus::kInvalidHpackIndex:
      return absl::InternalError(absl::StrFormat(
          "Invalid HPACK index received (%d)", invalid_hpack_index_));
    case HpackParseStatus::kIllegalHpackOpCode:
      return absl::InternalError("Illegal hpack op code");
  }
  GPR_UNREACHABLE_CODE(return absl::UnknownError("Should never reach here"));
}

}  // namespace grpc_core
