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

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <string>
#include <utility>

#include "src/core/call/metadata_info.h"
#include "src/core/ext/transport/chttp2/transport/header_assembler.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport.h"
#include "src/core/ext/transport/chttp2/transport/stream.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
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
  explicit IncomingMetadataTracker(Slice peer_string, const bool is_client)
      : peer_string_(std::move(peer_string)), is_client_(is_client) {}
  ~IncomingMetadataTracker() = default;

  IncomingMetadataTracker(IncomingMetadataTracker&& rvalue) = delete;
  IncomingMetadataTracker& operator=(IncomingMetadataTracker&& rvalue) = delete;
  IncomingMetadataTracker(const IncomingMetadataTracker&) = delete;
  IncomingMetadataTracker& operator=(const IncomingMetadataTracker&) = delete;

  static Slice GetPeerString(const PromiseEndpoint& endpoint) {
    absl::StatusOr<std::string> uri =
        grpc_event_engine::experimental::ResolvedAddressToURI(
            endpoint.GetPeerAddress());
    if (uri.ok()) {
      return Slice::FromCopiedString(*uri);
    }
    return Slice::FromCopiedString("unknown");
  }

  Slice peer_string() const { return peer_string_.Ref(); }

  void set_soft_limit(uint32_t limit) {
    max_header_list_size_soft_limit_ = limit;
  }
  uint32_t soft_limit() const { return max_header_list_size_soft_limit_; }

  HPackParser& parser() { return parser_; }

  void SetMaxHeaderTableSize(const uint32_t size) {
    parser_.hpack_table()->SetMaxBytes(size);
  }

  // This function is used to partially process a HEADER or CONTINUATION frame.
  // `PARTIAL PROCESSING` means reading the payload of a HEADER or CONTINUATION
  // and processing it with the HPACK decoder, and then discarding the payload.
  // This is done to keep the transports HPACK parser in sync with peers HPACK.
  // Scenarios where 'partial processing' is used when we receive a HEADER or
  // CONTINUATION frames when a stream is closed, or there is a Stream Error. We
  // do not do partial processing for Connection Errors because the Transport
  // will be destroyed soon after.
  Http2Status ParseAndDiscardHeaders(
      SliceBuffer&& buffer, const bool is_end_headers, Stream* stream,
      Http2Status&& original_status,
      const uint32_t max_header_list_size_hard_limit) {
    const HeaderAssembler::ParseHeaderArgs args = {
        /*is_initial_metadata=*/!incoming_header_end_stream_,
        /*is_end_headers=*/is_end_headers,
        /*is_client=*/is_client_,
        /*max_header_list_size_soft_limit=*/
        max_header_list_size_soft_limit_,
        /*max_header_list_size_hard_limit=*/max_header_list_size_hard_limit,
        /*stream_id=*/incoming_header_stream_id_,
    };
    GRPC_HTTP2_COMMON_DLOG << "ParseAndDiscardHeaders buffer "
                              "size: "
                           << buffer.Length() << " args: " << args.DebugString()
                           << " stream_id: "
                           << (stream == nullptr ? 0 : stream->GetStreamId())
                           << " original_status: "
                           << original_status.DebugString();
    if (stream != nullptr) {
      // Parse all the data in the header assembler
      Http2Status result = stream->GetHeaderAssembler().ParseAndDiscardHeaders(
          parser_, args.is_initial_metadata,
          args.max_header_list_size_soft_limit,
          args.max_header_list_size_hard_limit);
      if (!result.IsOk()) {
        GRPC_DCHECK(result.GetType() ==
                    Http2Status::Http2ErrorType::kConnectionError);
        LOG(ERROR) << "Connection Error: " << result;
        return result;
      }
    }

    if (buffer.Length() == 0) {
      return std::move(original_status);
    }

    Http2Status status = HeaderAssembler::ParseHeader(
        parser_, std::move(buffer), /*grpc_metadata_batch=*/nullptr, args);

    return (status.IsOk()) ? std::move(original_status) : std::move(status);
  }

  //////////////////////////////////////////////////////////////////////////////
  // Writing Header and Continuation State

  // Called when a HEADER frame is received.
  void UpdateState(const Http2HeaderFrame& frame) {
    GRPC_CHECK(!incoming_header_in_progress_);
    incoming_header_in_progress_ = !frame.end_headers;
    incoming_header_stream_id_ = frame.stream_id;
    incoming_header_end_stream_ = frame.end_stream;
  }

  // Called when a CONTINUATION frame is received.
  void UpdateState(const Http2ContinuationFrame& frame) {
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

  // A gRPC server is permitted to send both initial metadata and trailing
  // metadata where initial metadata is optional.
  // A gRPC C++ client is permitted to send only initial metadata.
  // However, other gRPC Client implementations may send trailing metadata too.
  // So we allow only a maximum of 2 metadata per streams.
  bool DidReceiveDuplicateMetadata(
      const bool did_receive_initial_metadata,
      const bool did_receive_trailing_metadata) const {
    const bool is_duplicate_initial_metadata =
        !incoming_header_end_stream_ && did_receive_initial_metadata;
    const bool is_duplicate_trailing_metadata =
        incoming_header_end_stream_ && did_receive_trailing_metadata;
    return is_duplicate_initial_metadata || is_duplicate_trailing_metadata;
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
  // Initialized only once at the time of transport creation.
  // Should remain constant for the lifetime of the transport.
  const Slice peer_string_;
  const bool is_client_;

  bool incoming_header_in_progress_ = false;
  bool incoming_header_end_stream_ = false;
  uint32_t incoming_header_stream_id_ = 0;
  uint32_t max_header_list_size_soft_limit_ =
      DEFAULT_MAX_HEADER_LIST_SIZE_SOFT_LIMIT;
  HPackParser parser_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_INCOMING_METADATA_TRACKER_H
