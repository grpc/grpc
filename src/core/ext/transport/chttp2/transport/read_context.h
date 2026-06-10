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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_READ_CONTEXT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_READ_CONTEXT_H

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

class ReadLoopPauseRestart {
 public:
  ReadLoopPauseRestart() = default;
  ReadLoopPauseRestart(const ReadLoopPauseRestart&) = delete;
  ReadLoopPauseRestart& operator=(const ReadLoopPauseRestart&) = delete;
  ReadLoopPauseRestart(ReadLoopPauseRestart&&) = delete;
  ReadLoopPauseRestart& operator=(ReadLoopPauseRestart&&) = delete;

  // Signals that the read loop should pause. If it's already paused, this is a
  // no-op.
  void SetPauseReadLoop() {
    // TODO(tjagtap) [PH2][P2][Settings] Plumb with when we receive urgent
    // settings. Example - initial window size 0 is urgent because it indicates
    // extreme memory pressure on the server.
    should_pause_read_loop_ = true;
  }

  // If SetPauseReadLoop() was called, this returns Pending and
  // registers a waker that will be woken by WakeReadLoop().
  // If the read loop does not need to be paused, this returns OkStatus.
  // This should be polled by the read loop to yield control when requested.
  Poll<absl::Status> MaybePauseReadLoop() {
    if (should_pause_read_loop_) {
      read_loop_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
      return Pending{};
    }
    return absl::OkStatus();
  }

  // If SetPauseReadLoop() was called, resumes ReadLoop by waking it up.
  // This will cause a wakeup ONLY if the loop was paused because of
  // MaybePauseReadLoop(). If the ReadLoop was paused due to other
  // endpoint.Read(), this wakeup will not happen.
  void ResumeReadLoopIfPaused() {
    if (should_pause_read_loop_) {
      should_pause_read_loop_ = false;
      read_loop_waker_.Wakeup();
    }
  }

  bool TestOnlyCheckCounters(const bool should_pause) const {
    return should_pause_read_loop_ == should_pause;
  }

  std::string DebugString() const {
    return absl::StrCat("{ should_pause_read_loop : ",
                        should_pause_read_loop_ ? "true }" : "false }");
  }

 private:
  bool should_pause_read_loop_ = false;
  Waker read_loop_waker_;
};

class IncomingMetadataState {
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
  IncomingMetadataState() = default;
  IncomingMetadataState(const IncomingMetadataState&) = delete;
  IncomingMetadataState& operator=(const IncomingMetadataState&) = delete;
  IncomingMetadataState(IncomingMetadataState&&) = delete;
  IncomingMetadataState& operator=(IncomingMetadataState&&) = delete;

  // Called when a HEADER frame is received.
  void UpdateState(const Http2HeaderFrame& frame) {
    GRPC_CHECK(!metadata_in_progress_);
    metadata_in_progress_ = !frame.end_headers;
    stream_id_ = frame.stream_id;
    end_stream_ = frame.end_stream;
  }

  // Called when a CONTINUATION frame is received.
  void UpdateState(const Http2ContinuationFrame& frame) {
    GRPC_CHECK(metadata_in_progress_);
    GRPC_CHECK_EQ(frame.stream_id, stream_id_);
    metadata_in_progress_ = !frame.end_headers;
  }

  // Returns true if we are in the middle of receiving a header block
  // (i.e., HEADERS without END_HEADERS was received, and we are waiting for
  // CONTINUATION frames).
  bool IsWaitingForContinuationFrame() const { return metadata_in_progress_; }

  // Returns true if end_stream was set in the received header.
  bool HeaderHasEndStream() const { return end_stream_; }

  // Returns stream id of stream for which headers are being received.
  uint32_t GetStreamId() const { return stream_id_; }

  // A gRPC server is permitted to send both initial metadata and trailing
  // metadata where initial metadata is optional.
  // A gRPC C++ client is permitted to send only initial metadata.
  // However, other gRPC Client implementations may send trailing metadata too.
  // So we allow only a maximum of 2 metadata per streams.
  bool DidReceiveDuplicateMetadata(
      const bool did_receive_initial_metadata,
      const bool did_receive_trailing_metadata) const {
    const bool is_duplicate_initial_metadata =
        !end_stream_ && did_receive_initial_metadata;
    const bool is_duplicate_trailing_metadata =
        end_stream_ && did_receive_trailing_metadata;
    return is_duplicate_initial_metadata || is_duplicate_trailing_metadata;
  }

  std::string DebugString() const {
    return absl::StrCat(
        "{ incoming_header_in_progress : ",
        metadata_in_progress_ ? "true" : "false",
        ", incoming_header_end_stream : ", end_stream_ ? "true" : "false",
        ", incoming_header_stream_id : ", stream_id_, "}");
  }

 private:
  bool metadata_in_progress_ = false;
  bool end_stream_ = false;
  uint32_t stream_id_ = 0;
};

class ReadContext {
 public:
  explicit ReadContext(const uint32_t max_new_streams_per_read_cycle,
                       const PromiseEndpoint& endpoint, const bool is_client)
      : max_new_streams_per_read_cycle_(max_new_streams_per_read_cycle),
        peer_string_(GetPeerString(endpoint)),
        is_client_(is_client),
        header_assembler_(is_client) {
    GRPC_DCHECK(max_new_streams_per_read_cycle > 0u)
        << "0 is invalid, because we will never be able to create a stream.";
  }
  ~ReadContext() = default;

  ReadContext(ReadContext&& rvalue) = delete;
  ReadContext& operator=(ReadContext&& rvalue) = delete;
  ReadContext(const ReadContext&) = delete;
  ReadContext& operator=(const ReadContext&) = delete;

  //////////////////////////////////////////////////////////////////////////////
  // Peer String management.

  Slice peer_string() const { return peer_string_.Ref(); }

  //////////////////////////////////////////////////////////////////////////////
  // HPack Parser Parsing and Management.

  void set_soft_limit(uint32_t limit) {
    max_header_list_size_soft_limit_ = limit;
  }
  uint32_t soft_limit() const { return max_header_list_size_soft_limit_; }

  uint32_t max_new_streams_per_read_cycle() const {
    return max_new_streams_per_read_cycle_;
  }

  HPackParser& parser() { return parser_; }
  HeaderAssembler& header_assembler() { return header_assembler_; }

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
      SliceBuffer&& buffer, const bool is_end_headers,
      Http2Status&& original_status,
      const uint32_t max_header_list_size_hard_limit) {
    const HeaderAssembler::ParseHeaderArgs args = {
        /*is_initial_metadata=*/!metadata_state_.HeaderHasEndStream(),
        /*is_end_headers=*/is_end_headers,
        /*is_client=*/is_client_,
        /*max_header_list_size_soft_limit=*/
        max_header_list_size_soft_limit_,
        /*max_header_list_size_hard_limit=*/max_header_list_size_hard_limit,
        /*stream_id=*/metadata_state_.GetStreamId(),
    };
    GRPC_HTTP2_COMMON_DLOG << "ParseAndDiscardHeaders buffer size: "
                           << buffer.Length() << " args: " << args.DebugString()
                           << " stream_id: " << metadata_state_.GetStreamId()
                           << " original_status: "
                           << original_status.DebugString();

    // Parse any data in the header assembler buffer
    Http2Status result = header_assembler_.ParseAndDiscardHeaders(
        parser_, args.is_initial_metadata, args.max_header_list_size_soft_limit,
        args.max_header_list_size_hard_limit);
    if (!result.IsOk()) {
      GRPC_DCHECK(result.GetType() ==
                  Http2Status::Http2ErrorType::kConnectionError);
      LOG(ERROR) << "Connection Error: " << result;
      return result;
    }

    if (buffer.Length() == 0) {
      return std::move(original_status);
    }

    Http2Status status = HeaderAssembler::ParseHeader(
        parser_, std::move(buffer), /*grpc_metadata_batch=*/nullptr, args);

    return (status.IsOk()) ? std::move(original_status) : std::move(status);
  }

  //////////////////////////////////////////////////////////////////////////////
  // Incoming Header and Continuation State

  // Client : For a client, the last_stream_id is the last stream that is sent
  // by the Client.
  // Server : For a server, the last_stream_id is the last known stream that is
  // received by the Server.
  Http2Status ValidateHeader(const uint32_t max_frame_size_setting,
                             const Http2FrameHeader& current_frame_header,
                             const uint32_t last_stream_id,
                             const bool is_first_settings_processed) {
    GRPC_HTTP2_COMMON_DLOG << "ReadContext::ValidateFrameHeader "
                           << current_frame_header.ToString();
    return ValidateFrameHeader(
        /*max_frame_size_setting=*/max_frame_size_setting,
        /*incoming_header_in_progress=*/
        metadata_state_.IsWaitingForContinuationFrame(),
        /*incoming_header_stream_id=*/metadata_state_.GetStreamId(),
        /*current_frame_header=*/current_frame_header,
        /*last_stream_id=*/last_stream_id,
        /*is_client=*/is_client_,
        /*is_first_settings_processed=*/is_first_settings_processed,
        /*tracker=*/tracker_);
  }

  // Called when a HEADER frame is received.
  void UpdateState(const Http2HeaderFrame& frame,
                   const bool is_existing_stream) {
    metadata_state_.UpdateState(frame);
    header_assembler_.SetStreamId(frame.stream_id);
    if (!is_client_ && !is_existing_stream) {
      // This is not relevant for clients, because only a client can initiate a
      // stream. Not a server.
      IncrementIncomingStreams();
    }
  }
  // Called when a CONTINUATION frame is received.
  void UpdateState(const Http2ContinuationFrame& frame,
                   GRPC_UNUSED const bool is_existing_stream) {
    metadata_state_.UpdateState(frame);
    if (frame.end_headers) {
      tracker_.OnLastContinuationFrame();
    }
  }

  // Returns true if we are in the middle of receiving a header block
  // (i.e., HEADERS without END_HEADERS was received, and we are waiting for
  // CONTINUATION frames).
  bool IsWaitingForContinuationFrame() const {
    return metadata_state_.IsWaitingForContinuationFrame();
  }

  // Returns true if end_stream was set in the received header.
  bool HeaderHasEndStream() const {
    return metadata_state_.HeaderHasEndStream();
  }

  // Returns stream id of stream for which headers are being received.
  uint32_t GetStreamId() const { return metadata_state_.GetStreamId(); }

  // A gRPC server is permitted to send both initial metadata and trailing
  // metadata where initial metadata is optional.
  // A gRPC C++ client is permitted to send only initial metadata.
  // However, other gRPC Client implementations may send trailing metadata too.
  // So we allow only a maximum of 2 metadata per streams.
  bool DidReceiveDuplicateMetadata(
      const bool did_receive_initial_metadata,
      const bool did_receive_trailing_metadata) const {
    return metadata_state_.DidReceiveDuplicateMetadata(
        did_receive_initial_metadata, did_receive_trailing_metadata);
  }

  Http2FrameCountTracker& mutable_tracker() { return tracker_; }
  const Http2FrameCountTracker& tracker() const { return tracker_; }

  //////////////////////////////////////////////////////////////////////////////
  // Current Frame Header management.

  const Http2FrameHeader& GetCurrentFrameHeader() const {
    return current_frame_header_;
  }
  void SetCurrentFrameHeader(const Http2FrameHeader& header) {
    current_frame_header_ = header;
    IncrementReadCycleCounters(header.length);
  }

  //////////////////////////////////////////////////////////////////////////////
  // ReadLoopPauseRestart wrapper functions.

  void SetPauseReadLoop() { read_loop_manager_.SetPauseReadLoop(); }

  Poll<absl::Status> MaybePauseReadLoop() {
    return read_loop_manager_.MaybePauseReadLoop();
  }

  void ResumeReadLoopIfPaused() {
    ResetReadCycleCounters();
    read_loop_manager_.ResumeReadLoopIfPaused();
  }

  bool TestOnlyCheckCounters(uint64_t expected_bytes_read,
                             uint16_t expected_read_count,
                             bool should_pause) const {
    return current_cycle_read_count_ == expected_read_count &&
           current_cycle_bytes_read_ == expected_bytes_read &&
           read_loop_manager_.TestOnlyCheckCounters(should_pause);
  }

  std::string DebugString() const {
    return absl::StrCat(
        "{ metadata_state : ", metadata_state_.DebugString(),
        ", read_loop_manager : ", read_loop_manager_.DebugString(),
        ", tracker : ", tracker_.DebugString(),
        ", current_cycle_num_new_streams : ", current_cycle_num_new_streams_,
        ", max_new_streams_per_read_cycle : ", max_new_streams_per_read_cycle_,
        ", current_cycle_bytes_read : ", current_cycle_bytes_read_,
        ", current_cycle_read_count : ", current_cycle_read_count_,
        ", current_frame_header : ", current_frame_header_.ToString(), "}");
  }

 private:
  static Slice GetPeerString(const PromiseEndpoint& endpoint) {
    absl::StatusOr<std::string> uri =
        grpc_event_engine::experimental::ResolvedAddressToURI(
            endpoint.GetPeerAddress());
    if (uri.ok()) {
      return Slice::FromCopiedString(*uri);
    }
    return Slice::FromCopiedString("unknown");
  }

  //////////////////////////////////////////////////////////////////////////////
  // Read Cycle Counter management.
  void ResetReadCycleCounters() {
    current_cycle_read_count_ = 0u;
    current_cycle_bytes_read_ = 0u;
    current_cycle_num_new_streams_ = 0u;
  }
  void IncrementIncomingStreams() {
    ++current_cycle_num_new_streams_;
    if (current_cycle_num_new_streams_ >= max_new_streams_per_read_cycle_) {
      read_loop_manager_.SetPauseReadLoop();
      ResetReadCycleCounters();
    }
  }
  void IncrementReadCycleCounters(const uint32_t payload_length) {
    current_cycle_bytes_read_ += kFrameHeaderSize + payload_length;
    ++current_cycle_read_count_;
    if (current_cycle_read_count_ >= kMaxFramesReadPerReadCycle) {
      read_loop_manager_.SetPauseReadLoop();
      ResetReadCycleCounters();
    }
  }

  // Counters to track total bytes and frames read per cycle.
  // Checked against limits to pause the read loop when maxed out.
  // This yields execution to prevent starvation of other transport tasks.
  // As per RFC 9113, HTTP/2 frame sizes can vary significantly.
  // Some frames are very large, while others are extremely small.
  // We stall the read loop based only on current_cycle_read_count_.
  // We measure current_cycle_bytes_read_ just for telemetry. We are not
  // stalling the read loop based on the number of bytes read right now because
  // we think that current_cycle_read_count_ would be sufficient for now.
  uint64_t current_cycle_bytes_read_ = 0u;
  uint16_t current_cycle_read_count_ = 0u;

  uint32_t current_cycle_num_new_streams_ = 0u;
  // Unlike other limits, this cannot be a constexpr because it is set per
  // transport via a ChannelArg named "grpc.http2.max_requests_per_read".
  const uint32_t max_new_streams_per_read_cycle_;

  // Initialized only once at the time of transport creation.
  // Should remain constant for the lifetime of the transport.
  const Slice peer_string_;
  const bool is_client_;

  uint32_t max_header_list_size_soft_limit_ =
      DEFAULT_MAX_HEADER_LIST_SIZE_SOFT_LIMIT;
  HPackParser parser_;
  Http2FrameCountTracker tracker_;
  Http2FrameHeader current_frame_header_ = {};
  ReadLoopPauseRestart read_loop_manager_;
  HeaderAssembler header_assembler_;
  IncomingMetadataState metadata_state_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_READ_CONTEXT_H
