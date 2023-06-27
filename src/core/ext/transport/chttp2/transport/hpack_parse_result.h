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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_PARSE_RESULT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HPACK_PARSE_RESULT_H

#include <stdint.h>

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {

// Result of parsing
// Makes it trivial to identify stream vs connection errors (via a range check)
enum class HpackParseStatus : uint8_t {
  ///////////////////////////////////////////////////////
  // Non-Errors

  // Parsed OK
  kOk,
  // Parse reached end of the current frame
  kEof,
  // Moved from - used to denote a HpackParseResult that has been moved into a
  // different object, and so the original should be deemed invalid.
  kMovedFrom,

  ///////////////////////////////////////////////////////////////////
  // Stream Errors - result in a stream cancellation

  // Sentinel value used to denote the first error that is a stream error.
  // All stream errors are hence >= kFirstStreamError and <
  // kFirstConnectionError.
  // Should not be used in switch statements, instead the first error message
  // after this one should be assigned the same value.
  kFirstStreamError,
  kInvalidMetadata = kFirstStreamError,
  // Hard metadata limit exceeded by the total set of metadata
  kHardMetadataLimitExceeded,
  kSoftMetadataLimitExceeded,
  // Hard metadata limit exceeded by a single key string
  kHardMetadataLimitExceededByKey,
  // Hard metadata limit exceeded by a single value string
  kHardMetadataLimitExceededByValue,
  kMetadataParseError,
  // Parse failed due to a base64 decode error
  kUnbase64Failed,

  ///////////////////////////////////////////////////////////////////
  // Connection Errors - result in the tcp connection closing

  // Sentinel value used to denote the first error that is a connection error.
  // All connection errors are hence >= kFirstConnectionError.
  // Should not be used in switch statements, instead the first error message
  // after this one should be assigned the same value.
  kFirstConnectionError,
  // Incomplete header at end of header boundary
  kIncompleteHeaderAtBoundary = kFirstConnectionError,
  // Varint out of range
  kVarintOutOfRange,
  // Invalid HPACK index
  kInvalidHpackIndex,
  // Illegal HPACK table size change
  kIllegalTableSizeChange,
  // Trying to add to the hpack table prior to reducing after a settings change
  kAddBeforeTableSizeUpdated,
  // Parse failed due to a huffman decode error
  kParseHuffFailed,
  // Too many dynamic table size changes in one frame
  kTooManyDynamicTableSizeChanges,
  // Maliciously long varint encoding
  // We don't read past 16 repeated 0x80 prefixes on a varint (all zeros)
  // because no reasonable varint encoder would emit that (16 is already quite
  // generous!)
  // Because we stop reading we don't parse the rest of the bytes and so we
  // can't recover parsing and would end up with a hpack table desync if we
  // tried, so this is a connection error.
  kMaliciousVarintEncoding,
  // Illegal hpack op code
  kIllegalHpackOpCode,
};

inline bool IsStreamError(HpackParseStatus status) {
  return status >= HpackParseStatus::kFirstStreamError &&
         status < HpackParseStatus::kFirstConnectionError;
}

inline bool IsConnectionError(HpackParseStatus status) {
  return status >= HpackParseStatus::kFirstConnectionError;
}

inline bool IsEphemeralError(HpackParseStatus status) {
  switch (status) {
    case HpackParseStatus::kSoftMetadataLimitExceeded:
    case HpackParseStatus::kHardMetadataLimitExceeded:
      return true;
    default:
      return false;
  }
}

class HpackParseResult {
 public:
  HpackParseResult() : HpackParseResult{HpackParseStatus::kOk} {}

  bool ok() const { return status_.get() == HpackParseStatus::kOk; }
  bool stream_error() const { return IsStreamError(status_.get()); }
  bool connection_error() const { return IsConnectionError(status_.get()); }
  bool ephemeral() const { return IsEphemeralError(status_.get()); }

  HpackParseResult PersistentStreamErrorOrOk() const {
    if (connection_error() || ephemeral()) return HpackParseResult();
    return *this;
  }

  static HpackParseResult FromStatus(HpackParseStatus status) {
    // Most statuses need some payloads, and we only need this functionality
    // rarely - so allow list the statuses that we can include here.
    switch (status) {
      case HpackParseStatus::kUnbase64Failed:
      case HpackParseStatus::kParseHuffFailed:
        return HpackParseResult{status};
      default:
        Crash(
            absl::StrCat("Invalid HpackParseStatus for FromStatus: ", status));
    }
  }

  static HpackParseResult FromStatusWithKey(HpackParseStatus status,
                                            absl::string_view key) {
    auto r = FromStatus(status);
    r.key_ = std::string(key);
    return r;
  }

  static HpackParseResult MetadataParseError(absl::string_view key) {
    HpackParseResult r{HpackParseStatus::kMetadataParseError};
    r.key_ = std::string(key);
    return r;
  }

  static HpackParseResult AddBeforeTableSizeUpdated(uint32_t current_size,
                                                    uint32_t max_size) {
    HpackParseResult p{HpackParseStatus::kAddBeforeTableSizeUpdated};
    p.illegal_table_size_change_ =
        IllegalTableSizeChange{current_size, max_size};
    return p;
  }

  static HpackParseResult MaliciousVarintEncodingError() {
    return HpackParseResult{HpackParseStatus::kMaliciousVarintEncoding};
  }

  static HpackParseResult IllegalHpackOpCode() {
    return HpackParseResult{HpackParseStatus::kIllegalHpackOpCode};
  }

  static HpackParseResult InvalidMetadataError(ValidateMetadataResult result,
                                               absl::string_view key) {
    GPR_DEBUG_ASSERT(result != ValidateMetadataResult::kOk);
    HpackParseResult p{HpackParseStatus::kInvalidMetadata};
    p.key_ = std::string(key);
    p.validate_metadata_result_ = result;
    return p;
  }

  static HpackParseResult IncompleteHeaderAtBoundaryError() {
    return HpackParseResult{HpackParseStatus::kIncompleteHeaderAtBoundary};
  }

  static HpackParseResult VarintOutOfRangeError(uint32_t value,
                                                uint8_t last_byte) {
    HpackParseResult p{HpackParseStatus::kVarintOutOfRange};
    p.varint_out_of_range_ = VarintOutOfRange{last_byte, value};
    return p;
  }

  static HpackParseResult InvalidHpackIndexError(uint32_t index) {
    HpackParseResult p{HpackParseStatus::kInvalidHpackIndex};
    p.invalid_hpack_index_ = index;
    return p;
  }

  static HpackParseResult IllegalTableSizeChangeError(uint32_t new_size,
                                                      uint32_t max_size) {
    HpackParseResult p{HpackParseStatus::kIllegalTableSizeChange};
    p.illegal_table_size_change_ = IllegalTableSizeChange{new_size, max_size};
    return p;
  }

  static HpackParseResult TooManyDynamicTableSizeChangesError() {
    return HpackParseResult{HpackParseStatus::kTooManyDynamicTableSizeChanges};
  }

  static HpackParseResult SoftMetadataLimitExceededError(
      grpc_metadata_batch* metadata, uint32_t frame_length, uint32_t limit) {
    HpackParseResult p{HpackParseStatus::kSoftMetadataLimitExceeded};
    p.metadata_limit_exceeded_ =
        MetadataLimitExceeded{frame_length, limit, metadata};
    return p;
  }

  static HpackParseResult HardMetadataLimitExceededError(
      grpc_metadata_batch* metadata, uint32_t frame_length, uint32_t limit) {
    HpackParseResult p{HpackParseStatus::kHardMetadataLimitExceeded};
    p.metadata_limit_exceeded_ =
        MetadataLimitExceeded{frame_length, limit, metadata};
    return p;
  }

  static HpackParseResult HardMetadataLimitExceededByKeyError(
      uint32_t key_length, uint32_t limit) {
    HpackParseResult p{HpackParseStatus::kHardMetadataLimitExceededByKey};
    p.metadata_limit_exceeded_by_atom_ =
        MetadataLimitExceededByAtom{key_length, limit};
    return p;
  }

  static HpackParseResult HardMetadataLimitExceededByValueError(
      absl::string_view key, uint32_t value_length, uint32_t limit) {
    HpackParseResult p{HpackParseStatus::kHardMetadataLimitExceededByValue};
    p.metadata_limit_exceeded_by_atom_ =
        MetadataLimitExceededByAtom{value_length, limit};
    p.key_ = std::string(key);
    return p;
  }

  // Compute the absl::Status that goes along with this HpackParseResult.
  // (may be cached, so this is not thread safe)
  absl::Status Materialize() const;

 private:
  explicit HpackParseResult(HpackParseStatus status) : status_(status) {}
  absl::Status BuildMaterialized() const;

  struct VarintOutOfRange {
    uint8_t last_byte;
    uint32_t value;
  };

  struct MetadataLimitExceeded {
    uint32_t frame_length;
    uint32_t limit;
    grpc_metadata_batch* prior;
  };

  // atom here means one of either a key or a value - so this is used for when a
  // metadata limit is consumed by either of these.
  struct MetadataLimitExceededByAtom {
    uint32_t atom_length;
    uint32_t limit;
  };

  struct IllegalTableSizeChange {
    uint32_t new_size;
    uint32_t max_size;
  };

  class StatusWrapper {
   public:
    explicit StatusWrapper(HpackParseStatus status) : status_(status) {}

    StatusWrapper(const StatusWrapper&) = default;
    StatusWrapper& operator=(const StatusWrapper&) = default;
    StatusWrapper(StatusWrapper&& other) noexcept
        : status_(std::exchange(other.status_, HpackParseStatus::kMovedFrom)) {}
    StatusWrapper& operator=(StatusWrapper&& other) noexcept {
      status_ = std::exchange(other.status_, HpackParseStatus::kMovedFrom);
      return *this;
    }

    HpackParseStatus get() const { return status_; }

   private:
    HpackParseStatus status_;
  };

  StatusWrapper status_;
  union {
    // Set if status == kInvalidMetadata
    ValidateMetadataResult validate_metadata_result_;
    // Set if status == kVarintOutOfRange
    VarintOutOfRange varint_out_of_range_;
    // Set if status == kInvalidHpackIndex
    uint32_t invalid_hpack_index_;
    // Set if status == kHardMetadataLimitExceeded or
    // kSoftMetadataLimitExceeded
    MetadataLimitExceeded metadata_limit_exceeded_;
    // Set if status == kHardMetadataLimitExceededByKey or
    // kHardMetadataLimitExceededByValue
    MetadataLimitExceededByAtom metadata_limit_exceeded_by_atom_;
    // Set if status == kIllegalTableSizeChange
    IllegalTableSizeChange illegal_table_size_change_;
  };
  std::string key_;
  mutable absl::optional<absl::Status> materialized_status_;
};

}  // namespace grpc_core

#endif
