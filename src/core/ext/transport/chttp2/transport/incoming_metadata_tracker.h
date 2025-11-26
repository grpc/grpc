//
//
// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INCOMING_METADATA_TRACKER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INCOMING_METADATA_TRACKER_H

#include <cstdint>
#include <string>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/util/grpc_check.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {
namespace http2 {

class IncomingMetadataTracker {
  // Manages transport-wide state for incoming HEADERS and CONTINUATION frames.
  // RFC 9113 (Section 6.10) requires that if a HEADERS frame does not have
  // END_HEADERS set, it must be followed by a contiguous sequence of
  // CONTINUATION frames for the same stream, ending with END_HEADERS. No other
  // frame types or frames for other streams may be interleaved during this
  // sequence. This constraint makes tracking header sequence state a
  // transport-level concern, as only one stream can be receiving headers at
  // a time. This class is distinct from HeaderAssembler, which buffers header
  // payloads on a per-stream basis.
 public:
  IncomingMetadataTracker() = default;
  ~IncomingMetadataTracker() = default;

  IncomingMetadataTracker(IncomingMetadataTracker&& rvalue) = delete;
  IncomingMetadataTracker& operator=(IncomingMetadataTracker&& rvalue) = delete;
  IncomingMetadataTracker(const IncomingMetadataTracker&) = delete;
  IncomingMetadataTracker& operator=(const IncomingMetadataTracker&) = delete;

  //////////////////////////////////////////////////////////////////////////////
  // Writing Header and Continuation State

  // Called when a HEADER frame is received.
  void OnHeaderReceived(const Http2HeaderFrame& frame) {
    GRPC_CHECK(!incoming_header_in_progress_);
    incoming_header_in_progress_ = !frame.end_headers;
    incoming_header_stream_id_ = frame.stream_id;
    incoming_header_end_stream_ = frame.end_stream;
  }

  // Called when a CONTINUATION frame is received.
  void OnContinuationReceived(const Http2ContinuationFrame& frame) {
    GRPC_CHECK(incoming_header_in_progress_);
    GRPC_CHECK_EQ(frame.stream_id, incoming_header_stream_id_);
    incoming_header_in_progress_ = !frame.end_headers;
  }

  //////////////////////////////////////////////////////////////////////////////
  // Reading Header and Continuation State

  // Returns true if we are in the middle of receiving a header block
  // (i.e., HEADERS without END_HEADERS was received, and we are waiting for
  // CONTINUATION frames).
  bool IsWaitingForContinuationFrame() const {
    return incoming_header_in_progress_;
  }

  // Returns true if end_stream was set in the received header.
  bool HeaderHasEndStream() const { return incoming_header_end_stream_; }

  // Returns stream id of stream for which headers are being received.
  uint32_t GetStreamId() const { return incoming_header_stream_id_; }

  bool ClientReceivedDuplicateMetadata(
      const bool did_receive_initial_metadata,
      const bool did_receive_trailing_metadata) const {
    const bool is_duplicate_initial_metadata =
        !incoming_header_end_stream_ && did_receive_initial_metadata;
    const bool is_duplicate_trailing_metadata =
        incoming_header_end_stream_ && did_receive_trailing_metadata;
    return is_duplicate_initial_metadata || is_duplicate_trailing_metadata;
  }

  bool ServerReceivedDuplicateMetadata(
      const bool did_receive_initial_metadata) const {
    // TODO(tjagtap) : [PH2][P2] : Verify this when implementing Server.
    // Also write a small unit test for it.
    return !incoming_header_end_stream_ && did_receive_initial_metadata;
  }

  std::string DebugString() const {
    return absl::StrCat(
        "{ incoming_header_in_progress : ",
        incoming_header_in_progress_ ? "true" : "false",
        ", incoming_header_end_stream : ",
        incoming_header_end_stream_ ? "true" : "false",
        ", incoming_header_stream_id : ", incoming_header_stream_id_, "}");
  }

 private:
  bool incoming_header_in_progress_ = false;
  bool incoming_header_end_stream_ = false;
  uint32_t incoming_header_stream_id_ = 0;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INCOMING_METADATA_TRACKER_H
