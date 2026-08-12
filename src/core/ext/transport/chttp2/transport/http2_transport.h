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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_TRANSPORT_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_TRANSPORT_H

#include <cstdint>
#include <string>
#include <type_traits>

#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/stream.h"
#include "src/core/ext/transport/chttp2/transport/write_cycle.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "absl/log/log.h"

namespace grpc_core {
namespace http2 {

// Experimental : This is just the initial skeleton of class
// and it is functions. The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P3] : Update the experimental status of the code when
// CHTTP2 is deleted.

#define GRPC_HTTP2_CLIENT_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

#define GRPC_HTTP2_SERVER_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

#define GRPC_HTTP2_CLIENT_ERROR_DLOG \
  LOG_IF(ERROR, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

#define GRPC_HTTP2_COMMON_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

constexpr uint32_t kGoawaySendTimeoutSeconds = 5u;

struct CloseStreamArgs {
  bool close_reads;
  bool close_writes;
};

inline bool ShouldEnablePh2Client() {
  return IsPh2ClientEnabled() || IsPh2ClientServerEnabled();
}

inline bool ShouldEnablePh2Server() {
  return IsPh2ServerEnabled() || IsPh2ClientServerEnabled();
}

// TODO(akshitpatel) [PH2][P5] : Write a way to measure the total size of a
// transport object. Reference :
// https://github.com/grpc/grpc/pull/41294/files#diff-c685cc4847f228327938326e2a45083a2d0845bacff0ac004bd802027a670c4e

///////////////////////////////////////////////////////////////////////////////
// Read and Write helpers

constexpr uint32_t kMaxFramesReadPerReadCycle = 16u * 1024u;  // 16K frames

Http2Status ValidateIncomingConnectionPreface(
    const absl::StatusOr<Slice>& status);

template <typename T, typename Tracker>
inline Http2Status ValidateMetadataFrameState(
    T& frame, Stream& stream, Tracker& incoming_headers,
    const uint32_t max_header_list_size) {
  if (stream.IsStreamHalfClosedRemote()) {
    return incoming_headers.ParseAndDiscardHeaders(
        std::move(frame.payload), frame.end_headers,
        Http2Status::Http2StreamError(
            Http2ErrorCode::kStreamClosed,
            std::string(RFC9113::kHalfClosedRemoteState)),
        max_header_list_size);
  }

  if constexpr (std::is_same_v<std::decay_t<T>, Http2HeaderFrame>) {
    if (incoming_headers.DidReceiveDuplicateMetadata(
            stream.IsInitialMetadataReceived(),
            stream.IsTrailingMetadataReceived())) {
      return incoming_headers.ParseAndDiscardHeaders(
          std::move(frame.payload), frame.end_headers,
          Http2Status::Http2StreamError(
              Http2ErrorCode::kInternalError,
              std::string(GrpcErrors::kTooManyMetadata)),
          max_header_list_size);
    }
  }

  return Http2Status::Ok();
}

///////////////////////////////////////////////////////////////////////////////
// Settings helpers

void InitLocalSettings(Http2Settings& settings, bool is_client);

////////////////////////////////////////////////////////////////////////////////
// Channel Args helpers

struct TransportChannelArgs {
  Duration keepalive_time;
  Duration keepalive_timeout;
  Duration ping_timeout;
  Duration settings_timeout;
  bool keepalive_permit_without_calls;
  // This is used to test peer behaviour when we never send a ping ack.
  bool test_only_ack_pings;
  uint32_t max_header_list_size_soft_limit;
  int max_usable_hpack_table_size;
  int initial_sequence_number;

  std::string DebugString() const;
};

void ReadChannelArgs(const ChannelArgs& channel_args,
                     TransportChannelArgs& args, Http2Settings& local_settings,
                     chttp2::TransportFlowControl& flow_control,
                     bool is_client);

void ReadSettingsFromChannelArgs(const ChannelArgs& channel_args,
                                 Http2Settings& local_settings,
                                 chttp2::TransportFlowControl& flow_control,
                                 bool is_client);

uint32_t MaxNewStreamsPerRead(const ChannelArgs& channel_args);

uint32_t GetMaxSecurityFrameSize(const ChannelArgs& channel_args);

// Returns the percentage of RST streams on which we should send a ping.
// Always returns 0 for client transport.
uint8_t GetPingOnRstStreamPercent(const ChannelArgs& channel_args,
                                  bool is_client);

///////////////////////////////////////////////////////////////////////////////
// ChannelZ helpers

RefCountedPtr<channelz::SocketNode> CreateChannelzSocketNode(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        event_engine_endpoint,
    const ChannelArgs& args);

///////////////////////////////////////////////////////////////////////////////
// Flow control helpers

void ProcessOutgoingDataFrameFlowControl(
    chttp2::StreamFlowControl& stream_flow_control,
    uint32_t flow_control_tokens_consumed);

ValueOrHttp2Status<chttp2::FlowControlAction>
ProcessIncomingDataFrameFlowControl(const Http2FrameHeader& frame,
                                    chttp2::TransportFlowControl& flow_control,
                                    Stream* stream);

// Returns true if a write should be triggered
bool ProcessIncomingWindowUpdateFrameFlowControl(
    const Http2WindowUpdateFrame& frame,
    chttp2::TransportFlowControl& flow_control, Stream* stream);

void MaybeAddTransportWindowUpdateFrame(
    chttp2::TransportFlowControl& flow_control, FrameSender& frame_sender);

void MaybeAddStreamWindowUpdateFrame(Stream& stream, FrameSender& frame_sender);

// ===========================================================================
// 3-Stage Transport Shutdown State Machine
// ===========================================================================
// Transport shutdown progresses through three distinct stages to ensure
// thread-safe, lock-free (where possible), and protocol-compliant cleanup.
//
// Stage 1: Shutdown Initiated
//   Triggered by MaybeSpawnCloseTransport(). Rejects new external watches
//   and immediately clears stream_list_ (Snapshot 1) to stop read/write
//   processing for active streams.
//   Note: MaybeSpawnCloseTransport is invoked from both transport party and
//   other threads (Orphan flow).
//
// Stage 2: Party Shutdown Initiated (Party Lockdown)
//   Triggered when the CloseTransport promise starts running on the transport
//   party. Thread-confined to the party (no locks/atomics needed).
//   Clears stream_list_ again (Snapshot 2) to capture raced streams, and
//   causes the transport to immediately reject any new or queued streams
//   (via InitializeStream() on client, or IncomingStream() on server).
//
// Stage 3: Shutdown Completed (Final Termination)
//   Triggered when CloseTransport() finishes (GOAWAY sent or timed out on
//   client). Wakes up and cancels all remaining promises on the transport
//   party.
// ===========================================================================
class TransportShutdownTracker {
 public:
  TransportShutdownTracker() = default;

  // Transport shutdown is not copyable or movable.
  TransportShutdownTracker(const TransportShutdownTracker&) = delete;
  TransportShutdownTracker& operator=(const TransportShutdownTracker&) = delete;
  TransportShutdownTracker(TransportShutdownTracker&&) = delete;
  TransportShutdownTracker& operator=(TransportShutdownTracker&&) = delete;

  // Stage 1: Shutdown Initiated
  void InitiateShutdown(const Mutex& mu) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu) {
    shutdown_initiated_ = true;
  }
  bool IsShutdownInitiated(const Mutex& mu) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu) {
    return shutdown_initiated_;
  }

  // Stage 2: Party Shutdown Initiated (Party Lockdown)
  void InitiatePartyShutdown() { party_shutdown_initiated_ = true; }
  bool IsPartyShutdownInitiated() const { return party_shutdown_initiated_; }

  // Stage 3: Shutdown Completed (Final Termination)
  void MarkShutdownComplete() { shutdown_completed_latch_.Set(); }
  auto WaitShutdownComplete() { return shutdown_completed_latch_.Wait(); }

 private:
  bool shutdown_initiated_ = false;
  bool party_shutdown_initiated_ = false;
  Latch<void> shutdown_completed_latch_;
};

//
}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_TRANSPORT_H
