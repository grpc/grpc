//
//
// Copyright 2024 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/http2_client_transport.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>
#include <limits.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/call/metadata_info.h"
#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/flow_control_manager.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/header_assembler.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport.h"
#include "src/core/ext/transport/chttp2/transport/http2_ztrace_collector.h"
#include "src/core/ext/transport/chttp2/transport/incoming_metadata_tracker.h"
#include "src/core/ext/transport/chttp2/transport/internal_channel_arg_names.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
#include "src/core/ext/transport/chttp2/transport/stream.h"
#include "src/core/ext/transport/chttp2/transport/stream_data_queue.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/match_promise.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/latent_see.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {
namespace http2 {

// As a gRPC server never initiates a stream, the last incoming stream id on
// the client side will always be 0.
constexpr uint32_t kLastIncomingStreamIdClient = 0;

using grpc_event_engine::experimental::EventEngine;
using EnqueueResult = StreamDataQueue<ClientMetadataHandle>::EnqueueResult;

// Experimental : This is just the initial skeleton of class
// and it is functions. The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P3] : Delete this comment when http2
// rollout begins

template <typename Factory>
void Http2ClientTransport::SpawnInfallibleTransportParty(absl::string_view name,
                                                         Factory&& factory) {
  general_party_->Spawn(name, std::forward<Factory>(factory), [](Empty) {});
}

template <typename Factory>
void Http2ClientTransport::SpawnGuardedTransportParty(absl::string_view name,
                                                      Factory&& factory) {
  general_party_->Spawn(
      name, std::forward<Factory>(factory),
      [self = RefAsSubclass<Http2ClientTransport>()](absl::Status status) {
        if (!status.ok()) {
          GRPC_UNUSED absl::Status error = self->HandleError(
              /*stream_id=*/std::nullopt,
              Http2Status::AbslConnectionError(status.code(),
                                               std::string(status.message())));
        }
      });
}

void Http2ClientTransport::PerformOp(grpc_transport_op* op) {
  // Notes : Refer : src/core/ext/transport/chaotic_good/client_transport.cc
  // Functions : StartConnectivityWatch, StopConnectivityWatch, PerformOp
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport PerformOp Begin";
  bool did_stuff = false;
  if (op->start_connectivity_watch != nullptr) {
    StartConnectivityWatch(op->start_connectivity_watch_state,
                           std::move(op->start_connectivity_watch));
    did_stuff = true;
  }
  if (op->stop_connectivity_watch != nullptr) {
    StopConnectivityWatch(op->stop_connectivity_watch);
    did_stuff = true;
  }
  GRPC_CHECK(!op->set_accept_stream)
      << "Set_accept_stream not supported on clients";
  GRPC_DCHECK(did_stuff) << "Unimplemented transport perform op ";

  ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport PerformOp End";
  // TODO(tjagtap) : [PH2][P2] :
  // Refer src/core/ext/transport/chttp2/transport/chttp2_transport.cc
  // perform_transport_op_locked
  // Maybe more operations needed to be implemented.
  // TODO(tjagtap) : [PH2][P2] : Consider either not using a transport level
  // lock, or making this run on the Transport party - whatever is better.
}

void Http2ClientTransport::StartConnectivityWatch(
    grpc_connectivity_state state,
    OrphanablePtr<ConnectivityStateWatcherInterface> watcher) {
  MutexLock lock(&transport_mutex_);
  state_tracker_.AddWatcher(state, std::move(watcher));
}

void Http2ClientTransport::StopConnectivityWatch(
    ConnectivityStateWatcherInterface* watcher) {
  MutexLock lock(&transport_mutex_);
  state_tracker_.RemoveWatcher(watcher);
}

void Http2ClientTransport::ReportDisconnection(
    const absl::Status& status, StateWatcher::DisconnectInfo disconnect_info,
    const char* reason) {
  MutexLock lock(&transport_mutex_);
  ReportDisconnectionLocked(status, disconnect_info, reason);
}

void Http2ClientTransport::ReportDisconnectionLocked(
    const absl::Status& status, StateWatcher::DisconnectInfo disconnect_info,
    const char* reason) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport ReportDisconnection: status="
                         << status.ToString() << "; reason=" << reason;
  state_tracker_.SetState(GRPC_CHANNEL_TRANSIENT_FAILURE, status, reason);
  NotifyStateWatcherOnDisconnectLocked(status, disconnect_info);
}

void Http2ClientTransport::StartWatch(RefCountedPtr<StateWatcher> watcher) {
  MutexLock lock(&transport_mutex_);
  GRPC_CHECK(watcher_ == nullptr);
  watcher_ = std::move(watcher);
  if (is_transport_closed_) {
    // TODO(tjagtap) : [PH2][P2] : Provide better status message and
    // disconnect info here.
    NotifyStateWatcherOnDisconnectLocked(
        absl::UnknownError("transport closed before watcher started"), {});
  } else {
    // TODO(tjagtap) : [PH2][P2] : Notify the state watcher of the current
    // value of the peer's MAX_CONCURRENT_STREAMS setting.
  }
}

void Http2ClientTransport::StopWatch(RefCountedPtr<StateWatcher> watcher) {
  MutexLock lock(&transport_mutex_);
  if (watcher_ == watcher) watcher_.reset();
}

void Http2ClientTransport::NotifyStateWatcherOnDisconnectLocked(
    absl::Status status, StateWatcher::DisconnectInfo disconnect_info) {
  if (watcher_ == nullptr) return;
  event_engine_->Run([watcher = std::move(watcher_), status = std::move(status),
                      disconnect_info]() mutable {
    ExecCtx exec_ctx;
    watcher->OnDisconnect(std::move(status), disconnect_info);
    watcher.reset();  // Before ExecCtx goes out of scope.
  });
}

void Http2ClientTransport::Orphan() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport Orphan Begin";
  // Accessing general_party here is not advisable. It may so happen that
  // the party is already freed/may free up any time. The only guarantee here
  // is that the transport is still valid.
  MaybeSpawnCloseTransport(Http2Status::AbslConnectionError(
      absl::StatusCode::kUnavailable, "Orphaned"));
  Unref();
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport Orphan End";
}

///////////////////////////////////////////////////////////////////////////////
// Processing each type of frame

Http2Status Http2ClientTransport::ProcessHttp2DataFrame(Http2DataFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-data
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ProcessHttp2DataFrame { stream_id="
      << frame.stream_id << ", end_stream=" << frame.end_stream
      << ", payload=" << frame.payload.JoinIntoString()
      << ", payload length=" << frame.payload.Length() << "}";

  // TODO(akshitpatel) : [PH2][P3] : Investigate if we should do this even if
  // the function returns a non-ok status?
  ping_manager_.ReceivedDataFrame();

  // Lookup stream
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ProcessHttp2DataFrame LookupStream";
  RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);

  ValueOrHttp2Status<chttp2::FlowControlAction> flow_control_action =
      ProcessIncomingDataFrameFlowControl(current_frame_header_, flow_control_,
                                          stream);
  if (!flow_control_action.IsOk()) {
    return ValueOrHttp2Status<chttp2::FlowControlAction>::TakeStatus(
        std::move(flow_control_action));
  }
  ActOnFlowControlAction(flow_control_action.value(), stream);

  if (stream == nullptr) {
    // TODO(tjagtap) : [PH2][P2] : Implement the correct behaviour later.
    // RFC9113 : If a DATA frame is received whose stream is not in the "open"
    // or "half-closed (local)" state, the recipient MUST respond with a stream
    // error (Section 5.4.2) of type STREAM_CLOSED.
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2DataFrame { stream_id="
        << frame.stream_id << "} Lookup Failed";
    return Http2Status::Ok();
  }

  if (stream->GetStreamState() == HttpStreamState::kHalfClosedRemote) {
    return Http2Status::Http2StreamError(
        Http2ErrorCode::kStreamClosed,
        std::string(RFC9113::kHalfClosedRemoteState));
  }

  // Add frame to assembler
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ProcessHttp2DataFrame AppendNewDataFrame";
  GrpcMessageAssembler& assembler = stream->assembler;
  Http2Status status =
      assembler.AppendNewDataFrame(frame.payload, frame.end_stream);
  if (!status.IsOk()) {
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessHttp2DataFrame "
                              "AppendNewDataFrame Failed";
    return status;
  }

  // Pass the messages up the stack if it is ready.
  while (true) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2DataFrame ExtractMessage";
    ValueOrHttp2Status<MessageHandle> result = assembler.ExtractMessage();
    if (!result.IsOk()) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport ProcessHttp2DataFrame ExtractMessage Failed";
      return ValueOrHttp2Status<MessageHandle>::TakeStatus(std::move(result));
    }
    MessageHandle message = TakeValue(std::move(result));
    if (message != nullptr) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport ProcessHttp2DataFrame SpawnPushMessage "
          << message->DebugString();
      stream->call.SpawnPushMessage(std::move(message));
      continue;
    }
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2DataFrame While Break";
    break;
  }

  // TODO(tjagtap) : [PH2][P2] : List of Tests:
  // 1. Data frame with unknown stream ID
  // 2. Data frame with only half a message and then end stream
  // 3. One data frame with a full message
  // 4. Three data frames with one full message
  // 5. One data frame with three full messages. All messages should be pushed.
  // Will need to mock the call_handler object and test this along with the
  // Header reading code. Because we need a stream in place for the lookup to
  // work.
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessHttp2HeaderFrame(
    Http2HeaderFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-headers
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ProcessHttp2HeaderFrame Promise { stream_id="
      << frame.stream_id << ", end_headers=" << frame.end_headers
      << ", end_stream=" << frame.end_stream
      << ", payload=" << frame.payload.JoinIntoString() << " }";
  ping_manager_.ReceivedDataFrame();
  incoming_header_in_progress_ = !frame.end_headers;
  incoming_header_stream_id_ = frame.stream_id;
  incoming_header_end_stream_ = frame.end_stream;

  RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
  if (stream == nullptr) {
    // TODO(tjagtap) : [PH2][P3] : Implement this.
    // RFC9113 : The identifier of a newly established stream MUST be
    // numerically greater than all streams that the initiating endpoint has
    // opened or reserved. This governs streams that are opened using a HEADERS
    // frame and streams that are reserved using PUSH_PROMISE. An endpoint that
    // receives an unexpected stream identifier MUST respond with a connection
    // error (Section 5.4.1) of type PROTOCOL_ERROR.
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2HeaderFrame Promise { stream_id="
        << frame.stream_id << "} Lookup Failed";
    return ParseAndDiscardHeaders(std::move(frame.payload),
                                  /*is_initial_metadata=*/!frame.end_stream,
                                  frame.end_headers, frame.stream_id,
                                  /*stream=*/nullptr, Http2Status::Ok());
  }

  if (stream->GetStreamState() == HttpStreamState::kHalfClosedRemote) {
    return ParseAndDiscardHeaders(
        std::move(frame.payload),
        /*is_initial_metadata=*/!frame.end_stream, frame.end_headers,
        frame.stream_id, stream,
        Http2Status::Http2StreamError(
            Http2ErrorCode::kStreamClosed,
            std::string(RFC9113::kHalfClosedRemoteState)));
  }

  if ((incoming_header_end_stream_ && stream->did_push_trailing_metadata) ||
      (!incoming_header_end_stream_ && stream->did_push_initial_metadata)) {
    return ParseAndDiscardHeaders(
        std::move(frame.payload),
        /*is_initial_metadata=*/!frame.end_stream, frame.end_headers,
        frame.stream_id, stream,
        Http2Status::Http2StreamError(Http2ErrorCode::kInternalError,
                                      "gRPC Error : A gRPC server can send "
                                      "upto 1 initial metadata followed "
                                      "by upto 1 trailing metadata"));
  }

  Http2Status append_result = stream->header_assembler.AppendHeaderFrame(frame);
  if (!append_result.IsOk()) {
    // Frame payload is not consumed if AppendHeaderFrame returns a non-OK
    // status. We need to process it to keep our in consistent state.
    return ParseAndDiscardHeaders(std::move(frame.payload),
                                  /*is_initial_metadata=*/!frame.end_stream,
                                  frame.end_headers, frame.stream_id, stream,
                                  std::move(append_result));
  }

  Http2Status status = ProcessMetadata(stream);
  if (!status.IsOk()) {
    // Frame payload has been moved to the HeaderAssembler. So calling
    // ParseAndDiscardHeaders with an empty buffer.
    return ParseAndDiscardHeaders(SliceBuffer(),
                                  /*is_initial_metadata=*/!frame.end_stream,
                                  frame.end_headers, frame.stream_id, stream,
                                  std::move(status));
  }

  // Frame payload has either been processed or moved to the HeaderAssembler.
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessMetadata(
    RefCountedPtr<Stream> stream) {
  HeaderAssembler& assembler = stream->header_assembler;
  CallHandler call = stream->call;

  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessMetadata";
  if (assembler.IsReady()) {
    ValueOrHttp2Status<ServerMetadataHandle> read_result =
        assembler.ReadMetadata(parser_, !incoming_header_end_stream_,
                               /*is_client=*/true,
                               /*max_header_list_size_soft_limit=*/
                               max_header_list_size_soft_limit_,
                               /*max_header_list_size_hard_limit=*/
                               settings_.acked().max_header_list_size());
    if (read_result.IsOk()) {
      ServerMetadataHandle metadata = TakeValue(std::move(read_result));
      if (incoming_header_end_stream_) {
        // TODO(tjagtap) : [PH2][P1] : Is this the right way to differentiate
        // between initial and trailing metadata?
        stream->MarkHalfClosedRemote();
        BeginCloseStream(stream, /*reset_stream_error_code=*/std::nullopt,
                         std::move(metadata));
      } else {
        GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessMetadata "
                                  "SpawnPushServerInitialMetadata";
        stream->did_push_initial_metadata = true;
        call.SpawnPushServerInitialMetadata(std::move(metadata));
      }
      return Http2Status::Ok();
    }
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessMetadata Failed";
    return ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>>::TakeStatus(
        std::move(read_result));
  }
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessHttp2RstStreamFrame(
    Http2RstStreamFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-rst_stream
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ProcessHttp2RstStreamFrame { stream_id="
      << frame.stream_id << ", error_code=" << frame.error_code << " }";
  Http2ErrorCode error_code = FrameErrorCodeToHttp2ErrorCode(frame.error_code);
  absl::Status status = absl::Status(ErrorCodeToAbslStatusCode(error_code),
                                     "Reset stream frame received.");
  RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
  if (stream != nullptr) {
    stream->MarkHalfClosedRemote();
    BeginCloseStream(stream, /*reset_stream_error_code=*/std::nullopt,
                     CancelledServerMetadataFromStatus(status));
  }

  // In case of stream error, we do not want the Read Loop to be broken. Hence
  // returning an ok status.
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessHttp2SettingsFrame(
    Http2SettingsFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-settings

  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ProcessHttp2SettingsFrame { ack=" << frame.ack
      << ", settings length=" << frame.settings.size() << "}";

  // The connector code needs us to run this
  // TODO(akshitpatel) : [PH2][P2] Move this to where settings are applied.
  if (on_receive_settings_ != nullptr) {
    event_engine_->Run(
        [on_receive_settings = std::move(on_receive_settings_)]() mutable {
          ExecCtx exec_ctx;
          std::move(on_receive_settings)(
              // TODO(tjagtap) : [PH2][P2] Send actual MAX_CONCURRENT_STREAMS
              // value here.
              std::numeric_limits<uint32_t>::max());
        });
  }

  // TODO(tjagtap) : [PH2][P2] Decide later if we want this only for AckLastSend
  // or does any other operation also need this lock.
  MutexLock lock(&transport_mutex_);
  if (!frame.ack) {
    // Check if the received settings have legal values
    Http2Status status = ValidateSettingsValues(frame.settings);
    if (!status.IsOk()) {
      return status;
    }
    // TODO(tjagtap) : [PH2][P1]
    // Apply the new settings
    // Quickly send the ACK to the peer once the settings are applied
    // When the peer changes MAX_CONCURRENT_STREAMS, notify the state watcher.
  } else {
    // Process the SETTINGS ACK Frame
    if (settings_.AckLastSend()) {
      // TODO(tjagtap) [PH2][P1][Settings] Fix this bug ASAP.
      // Causing DCHECKS to fail because of incomplete plumbing.
      // This is a bug.
      // transport_settings_.OnSettingsAckReceived();
    } else {
      // TODO(tjagtap) [PH2][P4] : The RFC does not say anything about what
      // should happen if we receive an unsolicited SETTINGS ACK. Decide if we
      // want to respond with any error or just proceed.
      LOG(ERROR) << "Settings ack received without sending settings";
    }
  }

  return Http2Status::Ok();
}

auto Http2ClientTransport::ProcessHttp2PingFrame(Http2PingFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-ping
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessHttp2PingFrame { ack="
                         << frame.ack << ", opaque=" << frame.opaque << " }";
  return AssertResultType<Http2Status>(If(
      frame.ack,
      [self = RefAsSubclass<Http2ClientTransport>(), opaque = frame.opaque]() {
        // Received a ping ack.
        return self->AckPing(opaque);
      },
      [self = RefAsSubclass<Http2ClientTransport>(), opaque = frame.opaque]() {
        // TODO(akshitpatel) : [PH2][P2] : Have a counter to track number of
        // pending induced frames (Ping/Settings Ack). This is to ensure that
        // if write is taking a long time, we can stop reads and prioritize
        // writes.
        // RFC9113: PING responses SHOULD be given higher priority than any
        // other frame.
        self->ping_manager_.AddPendingPingAck(opaque);
        // TODO(akshitpatel) : [PH2][P2] : This is done assuming that the other
        // ProcessFrame promises may return stream or connection failures. If
        // this does not turn out to be true, consider returning absl::Status
        // here.
        return Map(self->TriggerWriteCycle(), [](absl::Status status) {
          return (status.ok())
                     ? Http2Status::Ok()
                     : Http2Status::AbslConnectionError(
                           status.code(), std::string(status.message()));
        });
      }));
}

Http2Status Http2ClientTransport::ProcessHttp2GoawayFrame(
    Http2GoawayFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-goaway
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ProcessHttp2GoawayFrame Promise { "
         "last_stream_id="
      << frame.last_stream_id << ", error_code=" << frame.error_code
      << ", debug_data=" << frame.debug_data.as_string_view() << "}";
  LOG_IF(ERROR,
         frame.error_code != static_cast<uint32_t>(Http2ErrorCode::kNoError))
      << "Received GOAWAY frame with error code: " << frame.error_code
      << " and debug data: " << frame.debug_data.as_string_view();

  uint32_t last_stream_id = 0;
  absl::Status status(ErrorCodeToAbslStatusCode(
                          FrameErrorCodeToHttp2ErrorCode(frame.error_code)),
                      frame.debug_data.as_string_view());
  if (frame.error_code == static_cast<uint32_t>(Http2ErrorCode::kNoError) &&
      frame.last_stream_id == RFC9113::kMaxStreamId31Bit) {
    const uint32_t next_stream_id = PeekNextStreamId();
    last_stream_id = (next_stream_id > 1) ? next_stream_id - 2 : 0;
  } else {
    last_stream_id = frame.last_stream_id;
  }
  SetMaxAllowedStreamId(last_stream_id);

  bool close_transport = false;
  {
    MutexLock lock(&transport_mutex_);
    if (CanCloseTransportLocked()) {
      close_transport = true;
      GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessHttp2GoawayFrame "
                                "stream_list_ is empty";
    }
  }

  StateWatcher::DisconnectInfo disconnect_info;
  disconnect_info.reason = Transport::StateWatcher::kGoaway;
  disconnect_info.http2_error_code =
      static_cast<Http2ErrorCode>(frame.error_code);

  // Throttle keepalive time if the server sends a GOAWAY with error code
  // ENHANCE_YOUR_CALM and debug data equal to "too_many_pings". This will
  // apply to any new transport created on by any subchannel of this channel.
  if (GPR_UNLIKELY(frame.error_code == static_cast<uint32_t>(
                                           Http2ErrorCode::kEnhanceYourCalm) &&
                   frame.debug_data == "too_many_pings")) {
    LOG(ERROR) << ": Received a GOAWAY with error code ENHANCE_YOUR_CALM and "
                  "debug data equal to \"too_many_pings\". Current keepalive "
                  "time (before throttling): "
               << keepalive_time_.ToString();
    constexpr int max_keepalive_time_millis =
        INT_MAX / KEEPALIVE_TIME_BACKOFF_MULTIPLIER;
    uint64_t throttled_keepalive_time =
        keepalive_time_.millis() > max_keepalive_time_millis
            ? INT_MAX
            : keepalive_time_.millis() * KEEPALIVE_TIME_BACKOFF_MULTIPLIER;
    if (!IsTransportStateWatcherEnabled()) {
      status.SetPayload(kKeepaliveThrottlingKey,
                        absl::Cord(std::to_string(throttled_keepalive_time)));
    }
    disconnect_info.keepalive_time =
        Duration::Milliseconds(throttled_keepalive_time);
  }

  if (close_transport) {
    // TODO(akshitpatel) : [PH2][P3] : Ideally the error here should be
    // kNoError. However, Http2Status does not support kNoError. We should
    // revisit this and update the error code.
    MaybeSpawnCloseTransport(Http2Status::Http2ConnectionError(
        FrameErrorCodeToHttp2ErrorCode((
            frame.error_code ==
                    Http2ErrorCodeToFrameErrorCode(Http2ErrorCode::kNoError)
                ? Http2ErrorCodeToFrameErrorCode(Http2ErrorCode::kInternalError)
                : frame.error_code)),
        std::string(frame.debug_data.as_string_view())));
  }

  // lie: use transient failure from the transport to indicate goaway has been
  // received.
  ReportDisconnection(status, disconnect_info, "got_goaway");
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessHttp2WindowUpdateFrame(
    Http2WindowUpdateFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-window_update
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ProcessHttp2WindowUpdateFrame Promise { "
         " stream_id="
      << frame.stream_id << ", increment=" << frame.increment << "}";
  RefCountedPtr<Stream> stream = nullptr;
  if (frame.stream_id != 0) {
    stream = LookupStream(frame.stream_id);
  }
  bool should_trigger_write =
      ProcessIncomingWindowUpdateFrameFlowControl(frame, flow_control_, stream);
  if (should_trigger_write) {
    SpawnGuardedTransportParty("TransportTokensAvailable", TriggerWriteCycle());
  }
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessHttp2ContinuationFrame(
    Http2ContinuationFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-continuation
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ProcessHttp2ContinuationFrame Promise { "
         "stream_id="
      << frame.stream_id << ", end_headers=" << frame.end_headers
      << ", payload=" << frame.payload.JoinIntoString() << " }";
  incoming_header_in_progress_ = !frame.end_headers;
  RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
  if (stream == nullptr) {
    // TODO(tjagtap) : [PH2][P3] : Implement this.
    // RFC9113 : The identifier of a newly established stream MUST be
    // numerically greater than all streams that the initiating endpoint has
    // opened or reserved. This governs streams that are opened using a HEADERS
    // frame and streams that are reserved using PUSH_PROMISE. An endpoint that
    // receives an unexpected stream identifier MUST respond with a connection
    // error (Section 5.4.1) of type PROTOCOL_ERROR.
    return ParseAndDiscardHeaders(
        std::move(frame.payload),
        /*is_initial_metadata=*/!incoming_header_end_stream_,
        /*is_end_headers=*/frame.end_headers, frame.stream_id, nullptr,
        Http2Status::Ok());
  }

  if (stream->GetStreamState() == HttpStreamState::kHalfClosedRemote) {
    return ParseAndDiscardHeaders(
        std::move(frame.payload),
        /*is_initial_metadata=*/!incoming_header_end_stream_,
        /*is_end_headers=*/frame.end_headers, frame.stream_id, stream,
        Http2Status::Http2StreamError(
            Http2ErrorCode::kStreamClosed,
            std::string(RFC9113::kHalfClosedRemoteState)));
  }

  Http2Status append_result =
      stream->header_assembler.AppendContinuationFrame(frame);
  if (!append_result.IsOk()) {
    // Frame payload is not consumed if AppendContinuationFrame returns a
    // non-OK status. We need to process it to keep our in consistent state.
    return ParseAndDiscardHeaders(
        std::move(frame.payload),
        /*is_initial_metadata=*/!incoming_header_end_stream_,
        /*is_end_headers=*/frame.end_headers, frame.stream_id, stream,
        std::move(append_result));
  }

  Http2Status status = ProcessMetadata(stream);
  if (!status.IsOk()) {
    // Frame payload is consumed by HeaderAssembler. So passing an empty
    // SliceBuffer to ParseAndDiscardHeaders.
    return ParseAndDiscardHeaders(
        SliceBuffer(),
        /*is_initial_metadata=*/!incoming_header_end_stream_,
        /*is_end_headers=*/frame.end_headers, frame.stream_id, stream,
        std::move(status));
  }

  // Frame payload has either been processed or moved to the HeaderAssembler.
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessHttp2SecurityFrame(
    Http2SecurityFrame frame) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessHttp2SecurityFrame "
                            "{ payload="
                         << frame.payload.JoinIntoString() << " }";
  if ((settings_.acked().allow_security_frame() ||
       settings_.local().allow_security_frame()) &&
      settings_.peer().allow_security_frame()) {
    // TODO(tjagtap) : [PH2][P4] : Evaluate when to accept the frame and when to
    // reject it. Compare it with the requirement and with CHTTP2.
    // TODO(tjagtap) : [PH2][P3] : Add handling of Security frame
    // Just the frame.payload needs to be passed to the endpoint_ object.
    // Refer usage of TransportFramingEndpointExtension.
  }
  // Ignore the Security frame if it is not expected.
  return Http2Status::Ok();
}

auto Http2ClientTransport::ProcessOneFrame(Http2Frame frame) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessOneFrame Factory";
  return AssertResultType<Http2Status>(MatchPromise(
      std::move(frame),
      [self = RefAsSubclass<Http2ClientTransport>()](Http2DataFrame frame) {
        return self->ProcessHttp2DataFrame(std::move(frame));
      },
      [self = RefAsSubclass<Http2ClientTransport>()](Http2HeaderFrame frame) {
        return self->ProcessHttp2HeaderFrame(std::move(frame));
      },
      [self =
           RefAsSubclass<Http2ClientTransport>()](Http2RstStreamFrame frame) {
        return self->ProcessHttp2RstStreamFrame(frame);
      },
      [self = RefAsSubclass<Http2ClientTransport>()](Http2SettingsFrame frame) {
        return self->ProcessHttp2SettingsFrame(std::move(frame));
      },
      [self = RefAsSubclass<Http2ClientTransport>()](Http2PingFrame frame) {
        return self->ProcessHttp2PingFrame(frame);
      },
      [self = RefAsSubclass<Http2ClientTransport>()](Http2GoawayFrame frame) {
        return self->ProcessHttp2GoawayFrame(std::move(frame));
      },
      [self = RefAsSubclass<Http2ClientTransport>()](
          Http2WindowUpdateFrame frame) {
        return self->ProcessHttp2WindowUpdateFrame(frame);
      },
      [self = RefAsSubclass<Http2ClientTransport>()](
          Http2ContinuationFrame frame) {
        return self->ProcessHttp2ContinuationFrame(std::move(frame));
      },
      [self = RefAsSubclass<Http2ClientTransport>()](Http2SecurityFrame frame) {
        return self->ProcessHttp2SecurityFrame(std::move(frame));
      },
      [](GRPC_UNUSED Http2UnknownFrame frame) {
        // As per HTTP2 RFC, implementations MUST ignore and discard frames of
        // unknown types.
        return Http2Status::Ok();
      },
      [](GRPC_UNUSED Http2EmptyFrame frame) {
        LOG(DFATAL)
            << "ParseFramePayload should never return a Http2EmptyFrame";
        return Http2Status::Ok();
      }));
}

Http2Status Http2ClientTransport::ParseAndDiscardHeaders(
    SliceBuffer&& buffer, const bool is_initial_metadata,
    const bool is_end_headers, const uint32_t incoming_stream_id,
    const RefCountedPtr<Stream> stream, Http2Status&& original_status,
    DebugLocation whence) {
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ParseAndDiscardHeaders buffer "
         "size: "
      << buffer.Length() << " is_initial_metadata: " << is_initial_metadata
      << " is_end_headers: " << is_end_headers
      << " incoming_stream_id: " << incoming_stream_id
      << " stream_id: " << (stream == nullptr ? 0 : stream->GetStreamId())
      << " original_status: " << original_status.DebugString()
      << " whence: " << whence.file() << ":" << whence.line();

  return http2::ParseAndDiscardHeaders(
      parser_, std::move(buffer),
      HeaderAssembler::ParseHeaderArgs{
          /*is_initial_metadata=*/is_initial_metadata,
          /*is_end_headers=*/is_end_headers,
          /*is_client=*/true,
          /*max_header_list_size_soft_limit=*/
          max_header_list_size_soft_limit_,
          /*max_header_list_size_hard_limit=*/
          settings_.acked().max_header_list_size(),
          /*stream_id=*/incoming_stream_id,
      },
      stream, std::move(original_status));
}

///////////////////////////////////////////////////////////////////////////////
// Read Related Promises and Promise Factories

auto Http2ClientTransport::ReadAndProcessOneFrame() {
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ReadAndProcessOneFrame Factory";
  return AssertResultType<absl::Status>(TrySeq(
      // Fetch the first kFrameHeaderSize bytes of the Frame, these contain
      // the frame header.
      EndpointReadSlice(kFrameHeaderSize),
      // Parse the frame header.
      [](Slice header_bytes) -> Http2FrameHeader {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport ReadAndProcessOneFrame Parse "
            << header_bytes.as_string_view();
        return Http2FrameHeader::Parse(header_bytes.begin());
      },
      // Validate the incoming frame as per the current state of the transport
      [self = RefAsSubclass<Http2ClientTransport>()](Http2FrameHeader header) {
        Http2Status status = ValidateFrameHeader(
            /*max_frame_size_setting*/ self->settings_.acked().max_frame_size(),
            /*incoming_header_in_progress*/ self->incoming_header_in_progress_,
            /*incoming_header_stream_id*/ self->incoming_header_stream_id_,
            /*current_frame_header*/ header);

        if (GPR_UNLIKELY(!status.IsOk())) {
          GRPC_DCHECK(status.GetType() ==
                      Http2Status::Http2ErrorType::kConnectionError);
          return self->HandleError(std::nullopt, std::move(status));
        }
        GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport ReadAndProcessOneFrame "
                                  "Validated Frame Header:"
                               << header.ToString();
        self->current_frame_header_ = header;
        return absl::OkStatus();
      },
      // Read the payload of the frame.
      [self = RefAsSubclass<Http2ClientTransport>()]() {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport ReadAndProcessOneFrame Read Frame ";
        return AssertResultType<absl::StatusOr<SliceBuffer>>(
            self->EndpointRead(self->current_frame_header_.length));
      },
      // Parse the payload of the frame based on frame type.
      [self = RefAsSubclass<Http2ClientTransport>()](
          SliceBuffer payload) -> absl::StatusOr<Http2Frame> {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport ReadAndProcessOneFrame ParseFramePayload "
            << payload.JoinIntoString();
        ValueOrHttp2Status<Http2Frame> frame =
            ParseFramePayload(self->current_frame_header_, std::move(payload));
        if (!frame.IsOk()) {
          return self->HandleError(
              self->current_frame_header_.stream_id,
              ValueOrHttp2Status<Http2Frame>::TakeStatus(std::move(frame)));
        }
        return TakeValue(std::move(frame));
      },
      [self = RefAsSubclass<Http2ClientTransport>()](
          GRPC_UNUSED Http2Frame frame) {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport ReadAndProcessOneFrame ProcessOneFrame";
        return AssertResultType<absl::Status>(Map(
            self->ProcessOneFrame(std::move(frame)),
            [self](Http2Status status) {
              if (!status.IsOk()) {
                return self->HandleError(self->current_frame_header_.stream_id,
                                         std::move(status));
              }
              return absl::OkStatus();
            }));
      },
      [self = RefAsSubclass<Http2ClientTransport>()]() -> Poll<absl::Status> {
        if (self->should_stall_read_loop_) {
          self->read_loop_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
          return Pending{};
        }
        return absl::OkStatus();
      }));
}

auto Http2ClientTransport::ReadLoop() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport ReadLoop Factory";
  return AssertResultType<absl::Status>(
      Loop([self = RefAsSubclass<Http2ClientTransport>()]() {
        return TrySeq(self->ReadAndProcessOneFrame(),
                      []() -> LoopCtl<absl::Status> {
                        GRPC_HTTP2_CLIENT_DLOG
                            << "Http2ClientTransport ReadLoop Continue";
                        return Continue();
                      });
      }));
}

///////////////////////////////////////////////////////////////////////////////
// Flow Control for the Transport

auto Http2ClientTransport::FlowControlPeriodicUpdateLoop() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport PeriodicUpdateLoop Factory";
  return AssertResultType<absl::Status>(
      Loop([self = RefAsSubclass<Http2ClientTransport>()]() {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport FlowControlPeriodicUpdateLoop Loop";
        return TrySeq(
            // TODO(tjagtap) [PH2][P2][BDP] Remove this static sleep when the
            // BDP code is done.
            Sleep(chttp2::kFlowControlPeriodicUpdateTimer),
            [self]() -> Poll<absl::Status> {
              GRPC_HTTP2_CLIENT_DLOG
                  << "Http2ClientTransport FlowControl PeriodicUpdate()";
              chttp2::FlowControlAction action =
                  self->flow_control_.PeriodicUpdate();
              bool is_action_empty = action == chttp2::FlowControlAction();
              // This may trigger a write cycle
              self->ActOnFlowControlAction(action, nullptr);
              if (is_action_empty) {
                // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is
                // done. We must continue to do PeriodicUpdate once BDP is in
                // place.
                MutexLock lock(&self->transport_mutex_);
                if (self->GetActiveStreamCount() == 0) {
                  self->AddPeriodicUpdatePromiseWaker();
                  return Pending{};
                }
              }
              return absl::OkStatus();
            },
            [self]() -> LoopCtl<absl::Status> { return Continue{}; });
      }));
}

// Equivalent to grpc_chttp2_act_on_flowctl_action in chttp2_transport.cc
// TODO(tjagtap) : [PH2][P4] : grpc_chttp2_act_on_flowctl_action has a "reason"
// parameter which looks like it would be really helpful for debugging. Add that
void Http2ClientTransport::ActOnFlowControlAction(
    const chttp2::FlowControlAction& action, RefCountedPtr<Stream> stream) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::ActOnFlowControlAction";
  if (action.send_stream_update() != kNoActionNeeded) {
    if (GPR_LIKELY(stream != nullptr)) {
      GRPC_DCHECK_GT(stream->GetStreamId(), 0u);
      if (stream->CanSendWindowUpdateFrames()) {
        window_update_list_.insert(stream->GetStreamId());
      }
    } else {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport ActOnFlowControlAction stream is null";
    }
  }

  // TODO(tjagtap) : [PH2][P1] Plumb
  // enable_preferred_rx_crypto_frame_advertisement with settings
  ActOnFlowControlActionSettings(
      action, settings_.mutable_local(),
      /*enable_preferred_rx_crypto_frame_advertisement=*/true);

  if (action.AnyUpdateImmediately()) {
    // Prioritize sending flow control updates over reading data. If we
    // continue reading while urgent flow control updates are pending, we might
    // exhaust the flow control window. This prevents us from sending window
    // updates to the peer, causing the peer to block unnecessarily while
    // waiting for flow control tokens.
    should_stall_read_loop_ = true;
    SpawnGuardedTransportParty("SendControlFrames", TriggerWriteCycle());
  }
}

///////////////////////////////////////////////////////////////////////////////
// Write Related Promises and Promise Factories

auto Http2ClientTransport::WriteControlFrames() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport WriteControlFrames Factory";
  SliceBuffer output_buf;
  if (is_first_write_) {
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport WriteControlFrames "
                              "GRPC_CHTTP2_CLIENT_CONNECT_STRING";
    output_buf.Append(Slice(
        grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)));
    is_first_write_ = false;
    //  SETTINGS MUST be the first frame to be written onto a connection as per
    //  RFC9113.
    MaybeGetSettingsFrame(output_buf);
  }

  // Order of Control Frames is important.
  // 1. GOAWAY - This is first because if this is the final GoAway, then we may
  //             not need to send anything else to the peer.
  // 2. SETTINGS
  // 3. PING and PING acks.
  // 4. WINDOW_UPDATE
  // 5. Custom gRPC security frame

  goaway_manager_.MaybeGetSerializedGoawayFrame(output_buf);
  if (!goaway_manager_.IsImmediateGoAway()) {
    MaybeGetSettingsFrame(output_buf);
    ping_manager_.MaybeGetSerializedPingFrames(output_buf,
                                               NextAllowedPingInterval());
    MaybeGetWindowUpdateFrames(output_buf);
  }
  const uint64_t buffer_length = output_buf.Length();
  return If(
      buffer_length > 0,
      [self = RefAsSubclass<Http2ClientTransport>(),
       output_buf = std::move(output_buf), buffer_length]() mutable {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport WriteControlFrames Writing buffer of size "
            << buffer_length << " to endpoint";
        return self->endpoint_.Write(std::move(output_buf),
                                     PromiseEndpoint::WriteArgs{});
      },
      [self = RefAsSubclass<Http2ClientTransport>(), buffer_length] {
        self->ztrace_collector_->Append(
            PromiseEndpointWriteTrace{buffer_length});
        return absl::OkStatus();
      });
}

void Http2ClientTransport::NotifyControlFramesWriteDone() {
  // Notify Control modules that we have sent the frames.
  // All notifications are expected to be synchronous.
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport NotifyControlFramesWriteDone";
  if (should_stall_read_loop_) {
    should_stall_read_loop_ = false;
    read_loop_waker_.Wakeup();
  }
  ping_manager_.NotifyPingSent(ping_timeout_);
  goaway_manager_.NotifyGoawaySent();
}

auto Http2ClientTransport::SerializeAndWrite(std::vector<Http2Frame>&& frames) {
  SliceBuffer output_buf;
  should_reset_ping_clock_ =
      Serialize(absl::Span<Http2Frame>(frames), output_buf)
          .should_reset_ping_clock;
  size_t output_buf_length = output_buf.Length();
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport SerializeAndWrite Write "
                            "output_buf.length() = "
                         << output_buf_length;
  return AssertResultType<absl::Status>(If(
      output_buf_length > 0,
      [self = RefAsSubclass<Http2ClientTransport>(),
       output_buf = std::move(output_buf)]() mutable {
        return self->endpoint_.Write(std::move(output_buf),
                                     PromiseEndpoint::WriteArgs{});
      },
      []() { return absl::OkStatus(); }));
}

absl::StatusOr<std::vector<Http2Frame>>
Http2ClientTransport::DequeueStreamFrames(RefCountedPtr<Stream> stream) {
  // write_bytes_remaining_ is passed as an upper bound on the max
  // number of tokens that can be dequeued to prevent dequeuing huge
  // data frames when write_bytes_remaining_ is very low. As the
  // available transport tokens can only range from 0 to 2^31 - 1,
  // we are clamping the write_bytes_remaining_ to that range.
  const uint32_t max_dequeue_size =
      GetMaxPermittedDequeue(flow_control_, stream->flow_control,
                             write_bytes_remaining_, settings_.peer());
  stream->flow_control.ReportIfStalled(
      /*is_client=*/true, stream->GetStreamId(), settings_.peer());
  StreamDataQueue<ClientMetadataHandle>::DequeueResult result =
      stream->DequeueFrames(max_dequeue_size, settings_.peer().max_frame_size(),
                            encoder_);
  ProcessOutgoingDataFrameFlowControl(stream->flow_control,
                                      result.flow_control_tokens_consumed);
  if (result.is_writable) {
    // Stream is still writable. Enqueue it back to the writable
    // stream list.
    absl::Status status;
    if (AreTransportFlowControlTokensAvailable()) {
      status = writable_stream_list_.Enqueue(stream, result.priority);
    } else {
      status = writable_stream_list_.BlockedOnTransportFlowControl(stream);
    }

    if (GPR_UNLIKELY(!status.ok())) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport DequeueStreamFrames Failed to "
             "enqueue stream "
          << stream->GetStreamId() << " with status: " << status;
      // Close transport if we fail to enqueue stream.
      return HandleError(std::nullopt, Http2Status::AbslConnectionError(
                                           absl::StatusCode::kUnavailable,
                                           std::string(status.message())));
    }
  }

  // If the stream is aborted before initial metadata is dequeued, we will
  // not dequeue any frames from the stream data queue (including RST_STREAM).
  // Because of this, we will add the stream to the stream_list only when
  // we are guaranteed to send initial metadata on the wire. If the above
  // mentioned scenario occurs, the stream ref will be dropped by the
  // multiplexer loop as the stream will never be writable again. Additionally,
  // the other two stream refs, CallHandler OnDone and OutboundLoop will be
  // dropped by Callv3 triggering cleaning up the stream object.
  if (result.InitialMetadataDequeued()) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport DequeueStreamFrames InitialMetadataDequeued "
           "stream_id = "
        << stream->GetStreamId();
    stream->SentInitialMetadata();
    // After this point, initial metadata is guaranteed to be sent out.
    AddToStreamList(stream);
  }

  if (result.HalfCloseDequeued()) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport DequeueStreamFrames HalfCloseDequeued "
           "stream_id = "
        << stream->GetStreamId();
    stream->MarkHalfClosedLocal();
    CloseStream(stream, CloseStreamArgs{
                            /*close_reads=*/stream->did_push_trailing_metadata,
                            /*close_writes=*/true});
  }
  if (result.ResetStreamDequeued()) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport DequeueStreamFrames ResetStreamDequeued "
           "stream_id = "
        << stream->GetStreamId();
    stream->MarkHalfClosedLocal();
    CloseStream(stream, CloseStreamArgs{/*close_reads=*/true,
                                        /*close_writes=*/true});
  }

  // Update the write_bytes_remaining_ based on the bytes consumed
  // in the current dequeue.
  write_bytes_remaining_ =
      (write_bytes_remaining_ >= result.total_bytes_consumed)
          ? (write_bytes_remaining_ - result.total_bytes_consumed)
          : 0;
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport DequeueStreamFrames "
                            "write_bytes_remaining_ after dequeue = "
                         << write_bytes_remaining_ << " total_bytes_consumed = "
                         << result.total_bytes_consumed
                         << " stream_id = " << stream->GetStreamId()
                         << " is_writable = " << result.is_writable
                         << " stream_priority = "
                         << static_cast<uint8_t>(result.priority)
                         << " number of frames = " << result.frames.size();
  return std::move(result.frames);
}

// This MultiplexerLoop promise is responsible for Multiplexing multiple gRPC
// Requests (HTTP2 Streams) and writing them into one common endpoint.
auto Http2ClientTransport::MultiplexerLoop() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport MultiplexerLoop Factory";
  return AssertResultType<
      absl::Status>(Loop([self = RefAsSubclass<Http2ClientTransport>()]() {
    self->write_bytes_remaining_ = self->GetMaxWriteSize();
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport MultiplexerLoop "
                           << " max_write_size_=" << self->GetMaxWriteSize();
    return TrySeq(
        self->writable_stream_list_.WaitForReady(
            self->AreTransportFlowControlTokensAvailable()),
        [self]() {
          // TODO(akshitpatel) : [PH2][P2] : Return an `important` tag from
          // WriteControlFrames() to indicate if we should do a separate write
          // for the queued control frames or send the queued frames with the
          // data frames(if any).
          return Map(self->WriteControlFrames(), [self](absl::Status status) {
            if (GPR_UNLIKELY(!status.ok())) {
              GRPC_HTTP2_CLIENT_DLOG
                  << "Http2ClientTransport MultiplexerLoop Failed to "
                     "write control frames with status: "
                  << status;
              return status;
            }
            self->NotifyControlFramesWriteDone();
            return absl::OkStatus();
          });
        },
        [self]() -> absl::StatusOr<std::vector<Http2Frame>> {
          std::vector<Http2Frame> frames;
          // Drain all the writable streams till we have written
          // max_write_size_ bytes of data or there is no more data to send. In
          // some cases, we may write more than max_write_size_ bytes(like
          // writing metadata).
          while (self->write_bytes_remaining_ > 0) {
            std::optional<RefCountedPtr<Stream>> optional_stream =
                self->writable_stream_list_.ImmediateNext(
                    self->AreTransportFlowControlTokensAvailable());
            if (!optional_stream.has_value()) {
              GRPC_HTTP2_CLIENT_DLOG
                  << "Http2ClientTransport MultiplexerLoop "
                     "No writable streams available, write_bytes_remaining_ = "
                  << self->write_bytes_remaining_;
              break;
            }
            RefCountedPtr<Stream> stream = std::move(optional_stream.value());
            GRPC_HTTP2_CLIENT_DLOG
                << "Http2ClientTransport MultiplexerLoop "
                   "Next writable stream id = "
                << stream->GetStreamId()
                << " is_closed_for_writes = " << stream->IsClosedForWrites();

            if (stream->GetStreamId() == kInvalidStreamId) {
              GRPC_DCHECK(stream->GetStreamState() == HttpStreamState::kIdle);
              // TODO(akshitpatel) : [PH2][P4] : We will waste a stream id in
              // the rare scenario where the stream is aborted before it can be
              // written to. This is a possible area to optimize in future.
              absl::Status status = self->AssignStreamId(stream);
              if (!status.ok()) {
                GRPC_HTTP2_CLIENT_DLOG
                    << "Http2ClientTransport MultiplexerLoop "
                       "Failed to assign stream id and add to stream list for "
                       "stream: "
                    << stream.get() << " closing this stream.";
                self->BeginCloseStream(
                    stream, /*reset_stream_error_code=*/std::nullopt,
                    CancelledServerMetadataFromStatus(status));
                continue;
              }
            }

            if (GPR_LIKELY(!stream->IsClosedForWrites())) {
              auto stream_frames = self->DequeueStreamFrames(stream);
              if (GPR_UNLIKELY(!stream_frames.ok())) {
                GRPC_HTTP2_CLIENT_DLOG
                    << "Http2ClientTransport MultiplexerLoop "
                       "Failed to dequeue stream frames with status: "
                    << stream_frames.status();
                return stream_frames.status();
              }

              frames.reserve(frames.size() + stream_frames.value().size());
              frames.insert(
                  frames.end(),
                  std::make_move_iterator(stream_frames.value().begin()),
                  std::make_move_iterator(stream_frames.value().end()));
            }
          }

          GRPC_HTTP2_CLIENT_DLOG
              << "Http2ClientTransport MultiplexerLoop "
                 "write_bytes_remaining_ after draining all writable streams = "
              << self->write_bytes_remaining_;

          return std::move(frames);
        },
        [self](std::vector<Http2Frame> frames) {
          return self->SerializeAndWrite(std::move(frames));
        },
        [self]() -> LoopCtl<absl::Status> {
          if (self->should_reset_ping_clock_) {
            GRPC_HTTP2_CLIENT_DLOG
                << "Http2ClientTransport MultiplexerLoop ResetPingClock";
            self->ping_manager_.ResetPingClock(/*is_client=*/true);
            self->should_reset_ping_clock_ = false;
          }
          return Continue();
        });
  }));
}

absl::Status Http2ClientTransport::AssignStreamId(
    RefCountedPtr<Stream> stream) {
  absl::StatusOr<uint32_t> next_stream_id = NextStreamId();
  if (!next_stream_id.ok()) {
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport AssignStreamId "
                              "Failed to get next stream id for stream: "
                           << stream.get();
    return std::move(next_stream_id).status();
  }
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport AssignStreamId "
                            "Assigned stream id: "
                         << next_stream_id.value()
                         << " to stream: " << stream.get();
  stream->SetStreamId(next_stream_id.value());
  return absl::OkStatus();
}

void Http2ClientTransport::AddToStreamList(RefCountedPtr<Stream> stream) {
  bool should_wake_periodic_updates = false;
  {
    MutexLock lock(&transport_mutex_);
    GRPC_DCHECK(stream != nullptr) << "stream is null";
    GRPC_DCHECK_GT(stream->GetStreamId(), 0u) << "stream id is invalid";
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport AddToStreamList for stream id: "
        << stream->GetStreamId();
    stream_list_.emplace(stream->GetStreamId(), stream);
    // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
    if (GetActiveStreamCount() == 1) {
      should_wake_periodic_updates = true;
    }
  }
  // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
  if (should_wake_periodic_updates) {
    // Release the lock before you wake up another promise on the party.
    WakeupPeriodicUpdatePromise();
  }
}

void Http2ClientTransport::MaybeGetWindowUpdateFrames(SliceBuffer& output_buf) {
  std::vector<Http2Frame> frames;
  frames.reserve(window_update_list_.size() + 1);
  uint32_t window_size =
      flow_control_.DesiredAnnounceSize(/*writing_anyway=*/true);
  if (window_size > 0) {
    GRPC_HTTP2_CLIENT_DLOG << "Transport Window Update : " << window_size;
    frames.emplace_back(Http2WindowUpdateFrame{/*stream_id=*/0, window_size});
    flow_control_.SentUpdate(window_size);
  }
  for (const uint32_t stream_id : window_update_list_) {
    RefCountedPtr<Stream> stream = LookupStream(stream_id);
    if (stream != nullptr && stream->CanSendWindowUpdateFrames()) {
      const uint32_t increment = stream->flow_control.MaybeSendUpdate();
      if (increment > 0) {
        GRPC_HTTP2_CLIENT_DLOG << "Stream Window Update { " << stream_id << ", "
                               << window_size << " }";
        frames.emplace_back(Http2WindowUpdateFrame{stream_id, increment});
      }
    }
  }
  window_update_list_.clear();
  if (!frames.empty()) {
    GRPC_HTTP2_CLIENT_DLOG << "Total Window Update Frames : " << frames.size();
    Serialize(absl::Span<Http2Frame>(frames), output_buf);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Constructor Destructor

Http2ClientTransport::Http2ClientTransport(
    PromiseEndpoint endpoint, GRPC_UNUSED const ChannelArgs& channel_args,
    std::shared_ptr<EventEngine> event_engine,
    absl::AnyInvocable<void(absl::StatusOr<uint32_t>)> on_receive_settings)
    : channelz::DataSource(http2::CreateChannelzSocketNode(
          endpoint.GetEventEngineEndpoint(), channel_args)),
      event_engine_(std::move(event_engine)),
      endpoint_(std::move(endpoint)),
      next_stream_id_(/*Initial Stream ID*/ 1),
      should_reset_ping_clock_(false),
      incoming_header_in_progress_(false),
      incoming_header_end_stream_(false),
      is_first_write_(true),
      incoming_header_stream_id_(0),
      on_receive_settings_(std::move(on_receive_settings)),
      max_header_list_size_soft_limit_(
          GetSoftLimitFromChannelArgs(channel_args)),
      max_write_size_(kMaxWriteSize),
      keepalive_time_(std::max(
          Duration::Seconds(10),
          channel_args.GetDurationFromIntMillis(GRPC_ARG_KEEPALIVE_TIME_MS)
              .value_or(Duration::Infinity()))),
      // Keepalive timeout is only passed to the keepalive manager if it is less
      // than the ping timeout. As keepalives use pings for health checks, if
      // keepalive timeout is greater than ping timeout, we would always hit the
      // ping timeout first.
      keepalive_timeout_(std::max(
          Duration::Zero(),
          channel_args.GetDurationFromIntMillis(GRPC_ARG_KEEPALIVE_TIMEOUT_MS)
              .value_or(keepalive_time_ == Duration::Infinity()
                            ? Duration::Infinity()
                            : (Duration::Seconds(20))))),
      ping_timeout_(std::max(
          Duration::Zero(),
          channel_args.GetDurationFromIntMillis(GRPC_ARG_PING_TIMEOUT_MS)
              .value_or(keepalive_time_ == Duration::Infinity()
                            ? Duration::Infinity()
                            : Duration::Minutes(1)))),
      ping_manager_(channel_args, PingSystemInterfaceImpl::Make(this),
                    event_engine_),
      keepalive_manager_(
          KeepAliveInterfaceImpl::Make(this),
          ((keepalive_timeout_ < ping_timeout_) ? keepalive_timeout_
                                                : Duration::Infinity()),
          keepalive_time_),
      keepalive_permit_without_calls_(
          channel_args.GetBool(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS)
              .value_or(false)),
      goaway_manager_(GoawayInterfaceImpl::Make(this)),
      enable_preferred_rx_crypto_frame_advertisement_(
          channel_args
              .GetBool(GRPC_ARG_EXPERIMENTAL_HTTP2_PREFERRED_CRYPTO_FRAME_SIZE)
              .value_or(false)),
      memory_owner_(channel_args.GetObject<ResourceQuota>()
                        ->memory_quota()
                        ->CreateMemoryOwner()),
      flow_control_(
          "PH2_Client",
          channel_args.GetBool(GRPC_ARG_HTTP2_BDP_PROBE).value_or(true),
          &memory_owner_),
      ztrace_collector_(std::make_shared<PromiseHttp2ZTraceCollector>()),
      should_stall_read_loop_(false) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport Constructor Begin";
  SourceConstructed();

  InitLocalSettings(settings_.mutable_local(), /*is_client=*/true);
  ReadSettingsFromChannelArgs(channel_args, settings_.mutable_local(),
                              flow_control_, /*is_client=*/true);

  // Initialize the general party and write party.
  auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
  general_party_arena->SetContext<EventEngine>(event_engine_.get());
  general_party_ = Party::Make(std::move(general_party_arena));

  // The keepalive loop is only spawned if the keepalive time is not infinity.
  keepalive_manager_.Spawn(general_party_.get());

  const int max_hpack_table_size =
      channel_args.GetInt(GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER).value_or(-1);
  if (max_hpack_table_size >= 0) {
    encoder_.SetMaxUsableSize(max_hpack_table_size);
  }

  transport_settings_.SetSettingsTimeout(channel_args, keepalive_timeout_);

  if (settings_.local().allow_security_frame()) {
    // TODO(tjagtap) : [PH2][P3] : Setup the plumbing to pass the security frame
    // to the endpoing via TransportFramingEndpointExtension.
    // Also decide if this plumbing is done here, or when the peer sends
    // allow_security_frame too.
  }

  // Spawn a promise to flush the gRPC initial connection string and settings
  // frames.
  SpawnGuardedTransportParty("FlushInitialFrames", TriggerWriteCycle());
  SpawnGuardedTransportParty("ReadLoop", UntilTransportClosed(ReadLoop()));
  SpawnGuardedTransportParty("MultiplexerLoop",
                             UntilTransportClosed(MultiplexerLoop()));
  SpawnGuardedTransportParty(
      "FlowControlPeriodicUpdateLoop",
      UntilTransportClosed(FlowControlPeriodicUpdateLoop()));
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport Constructor End";
}

// This function MUST be idempotent. This function MUST be called from the
// transport party.
void Http2ClientTransport::CloseStream(RefCountedPtr<Stream> stream,
                                       CloseStreamArgs args,
                                       DebugLocation whence) {
  std::optional<Http2Status> close_transport_error;

  {
    // TODO(akshitpatel) : [PH2][P3] : Measure the impact of holding mutex
    // throughout this function.
    MutexLock lock(&transport_mutex_);
    GRPC_DCHECK(stream != nullptr) << "stream is null";
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::CloseStream for stream id: "
        << stream->GetStreamId() << " close_reads=" << args.close_reads
        << " close_writes=" << args.close_writes
        << " incoming_header_in_progress=" << incoming_header_in_progress_
        << " location=" << whence.file() << ":" << whence.line();

    if (args.close_writes) {
      stream->SetWriteClosed();
    }

    if (args.close_reads) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::CloseStream for stream id: "
          << stream->GetStreamId() << " closing stream for reads.";
      // If the stream is closed while reading HEADER/CONTINUATION frames, we
      // should still parse the enqueued buffer to maintain HPACK state between
      // peers.
      if (incoming_header_in_progress_) {
        incoming_header_in_progress_ = false;
        Http2Status result = http2::ParseAndDiscardHeaders(
            parser_, SliceBuffer(),
            HeaderAssembler::ParseHeaderArgs{
                /*is_initial_metadata=*/!incoming_header_end_stream_,
                /*is_end_headers=*/false,
                /*is_client=*/true,
                /*max_header_list_size_soft_limit=*/
                max_header_list_size_soft_limit_,
                /*max_header_list_size_hard_limit=*/
                settings_.acked().max_header_list_size(),
                /*stream_id=*/incoming_header_stream_id_,
            },
            stream, /*original_status=*/Http2Status::Ok());
        if (!result.IsOk() &&
            result.GetType() == Http2Status::Http2ErrorType::kConnectionError) {
          GRPC_HTTP2_CLIENT_DLOG
              << "Http2ClientTransport::CloseStream for stream id: "
              << stream->GetStreamId()
              << " failed to partially process header: "
              << result.DebugString();
          close_transport_error.emplace(std::move(result));
        }
      }

      stream_list_.erase(stream->GetStreamId());
      if (!close_transport_error.has_value() && CanCloseTransportLocked()) {
        // TODO(akshitpatel) : [PH2][P3] : Is kInternalError the right error
        // code to use here? IMO it should be kNoError.
        close_transport_error.emplace(Http2Status::Http2ConnectionError(
            Http2ErrorCode::kInternalError,
            "Received GOAWAY frame and no more streams to close."));
      }
    }
  }

  if (close_transport_error.has_value()) {
    GRPC_UNUSED absl::Status status = HandleError(
        /*stream_id=*/std::nullopt, std::move(*close_transport_error));
  }
}

// This function is idempotent and MUST be called from the transport party.
// All the scenarios that can lead to this function being called are:
// 1. Reading a RST stream frame: In this case, the stream is immediately
//    closed for reads and writes and removed from the stream_list_.
// 2. Reading a Trailing Metadata frame: There are two possible scenarios:
//    a. The stream is closed for writes: Close the stream for reads and writes
//       and remove the stream from the stream_list_.
//    b. The stream is NOT closed for writes: Stream is kept open for reads and
//       writes. CallHandler OnDone will trigger sending a half close frame. If
//       before the multiplexer loop triggers sending a half close a RST stream
//       is read, the stream is closed for reads and writes immediately and the
//       half close is discarded. If no RST stream is read, the stream is closed
//       for reads and writes upon sending the half close frame from the
//       multiplexer loop.
// 3. Hitting error condition in the transport: In this case, RST stream is
//    enqueued and the stream is closed for reads immediately. This implies we
//    reduce the number of active streams inline. When multiplexer loop
//    processes the RST stream frame, the stream ref will dropped. The other
//    stream ref will be dropped when CallHandler's OnDone is executed causing
//    the stream to be destroyed. CallHandlers OnDone also tries to enqueue a
//    RST stream frame. This is a no-op at this point.
// 4. Application abort: In this case, CallHandler OnDone will enqueue RST
//    stream frame to the stream data queue. The multiplexer loop will send the
//    reset stream frame and close the stream from reads and writes.
// 5. Transport close: This takes up the same path as case 3.
// In all the above cases, trailing metadata is pushed to the call spine.
// Note: The stream ref is held in atmost 3 places:
// 1. stream_list_ : This is released when the stream is closed for reads.
// 2. CallHandler OnDone : This is released when Trailing Metadata is pushed to
//    the call spine.
// 3. List of writable streams : This is released after the final frame is
//    dequeued from the StreamDataQueue.
void Http2ClientTransport::BeginCloseStream(
    RefCountedPtr<Stream> stream,
    std::optional<uint32_t> reset_stream_error_code,
    ServerMetadataHandle&& metadata, DebugLocation whence) {
  if (stream == nullptr) {
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::BeginCloseStream stream "
                              "is null reset_stream_error_code="
                           << (reset_stream_error_code.has_value()
                                   ? absl::StrCat(*reset_stream_error_code)
                                   : "nullopt")
                           << " metadata=" << metadata->DebugString();
    return;
  }

  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::BeginCloseStream for stream id: "
      << stream->GetStreamId() << " error_code="
      << (reset_stream_error_code.has_value()
              ? absl::StrCat(*reset_stream_error_code)
              : "nullopt")
      << " ServerMetadata=" << metadata->DebugString()
      << " location=" << whence.file() << ":" << whence.line();

  bool close_reads = false;
  bool close_writes = false;
  if (metadata->get(GrpcCallWasCancelled())) {
    if (!reset_stream_error_code) {
      // Callers taking this path:
      // 1. Reading a RST stream frame (will not send any frame out).
      close_reads = true;
      close_writes = true;
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::BeginCloseStream for stream id: "
          << stream->GetStreamId() << " close_reads= " << close_reads
          << " close_writes= " << close_writes;
    } else {
      // Callers taking this path:
      // 1. Processing Error in transport (will send reset stream from here).
      absl::StatusOr<EnqueueResult> enqueue_result =
          stream->EnqueueResetStream(reset_stream_error_code.value());
      GRPC_HTTP2_CLIENT_DLOG << "Enqueued ResetStream with error code="
                             << reset_stream_error_code.value()
                             << " status=" << enqueue_result.status();
      if (enqueue_result.ok()) {
        GRPC_UNUSED absl::Status status =
            MaybeAddStreamToWritableStreamList(stream, enqueue_result.value());
      }
      close_reads = true;
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::BeginCloseStream for stream id: "
          << stream->GetStreamId() << " close_reads= " << close_reads
          << " close_writes= " << close_writes;
    }
  } else {
    // Callers taking this path:
    // 1. Reading Trailing Metadata (MAY send half close from OnDone).
    if (stream->IsClosedForWrites()) {
      close_reads = true;
      close_writes = true;
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::BeginCloseStream for stream id: "
          << stream->GetStreamId() << " close_reads= " << close_reads
          << " close_writes= " << close_writes;
    }
  }

  if (close_reads || close_writes) {
    CloseStream(stream, CloseStreamArgs{close_reads, close_writes}, whence);
  }

  stream->did_push_trailing_metadata = true;
  // This maybe called multiple times while closing a stream. This should be
  // fine as the the call spine ignores the subsequent calls.
  stream->call.SpawnPushServerTrailingMetadata(std::move(metadata));
}

void Http2ClientTransport::CloseTransport() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::CloseTransport";

  transport_closed_latch_.Set();
  // If some scenario causes the transport to close without ever receiving
  // settings, we need to still invoke the closure passed to the transport.
  // Additionally, as this function will always run on the transport party, it
  // cannot race with reading a settings frame.
  // TODO(akshitpatel): [PH2][P2] Pass the actual error that caused the
  // transport to be closed here.
  if (on_receive_settings_ != nullptr) {
    event_engine_->Run(
        [on_receive_settings = std::move(on_receive_settings_)]() mutable {
          ExecCtx exec_ctx;
          std::move(on_receive_settings)(
              absl::UnavailableError("transport closed"));
        });
  }

  MutexLock lock(&transport_mutex_);
  // This is the only place where the general_party_ is
  // reset.
  general_party_.reset();
}

void Http2ClientTransport::MaybeSpawnCloseTransport(Http2Status http2_status,
                                                    DebugLocation whence) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::MaybeSpawnCloseTransport "
                            "status="
                         << http2_status << " location=" << whence.file() << ":"
                         << whence.line();

  // Free up the stream_list at this point. This would still allow the frames
  // in the MPSC to be drained and block any additional frames from being
  // enqueued. Additionally this also prevents additional frames with non-zero
  // stream_ids from being processed by the read loop.
  ReleasableMutexLock lock(&transport_mutex_);
  if (is_transport_closed_) {
    lock.Release();
    return;
  }
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::MaybeSpawnCloseTransport "
                            "Initiating transport close";
  is_transport_closed_ = true;
  absl::flat_hash_map<uint32_t, RefCountedPtr<Stream>> stream_list =
      std::move(stream_list_);
  stream_list_.clear();
  // TODO(tjagtap) : [PH2][P2] : Provide better disconnect info here.
  ReportDisconnectionLocked(http2_status.GetAbslConnectionError(), {},
                            "transport closed");
  lock.Release();

  SpawnInfallibleTransportParty(
      "CloseTransport", [self = RefAsSubclass<Http2ClientTransport>(),
                         stream_list = std::move(stream_list),
                         http2_status = std::move(http2_status)]() mutable {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::CloseTransport Cleaning up call stacks";
        // Clean up the call stacks for all active streams.
        for (const auto& pair : stream_list) {
          // There is no merit in transitioning the stream to
          // closed state here as the subsequent lookups would
          // fail. Also, as this is running on the transport
          // party, there would not be concurrent access to the stream.
          auto& stream = pair.second;
          self->BeginCloseStream(stream,
                                 Http2ErrorCodeToFrameErrorCode(
                                     http2_status.GetConnectionErrorCode()),
                                 CancelledServerMetadataFromStatus(
                                     http2_status.GetAbslConnectionError()));
        }

        // RFC9113 : A GOAWAY frame might not immediately precede closing of
        // the connection; a receiver of a GOAWAY that has no more use for the
        // connection SHOULD still send a GOAWAY frame before terminating the
        // connection.
        return Map(
            // TODO(akshitpatel) : [PH2][P4] : This is creating a copy of
            // the debug data. Verify if this is causing a performance
            // issue.
            Race(AssertResultType<absl::Status>(
                     self->goaway_manager_.RequestGoaway(
                         http2_status.GetConnectionErrorCode(),
                         /*debug_data=*/
                         Slice::FromCopiedString(
                             http2_status.GetAbslConnectionError().message()),
                         kLastIncomingStreamIdClient, /*immediate=*/true)),
                 // Failsafe to close the transport if goaway is not
                 // sent within kGoawaySendTimeoutSeconds seconds.
                 Sleep(Duration::Seconds(kGoawaySendTimeoutSeconds))),
            [self](auto) mutable {
              self->CloseTransport();
              return Empty{};
            });
        ;
      });
}

bool Http2ClientTransport::CanCloseTransportLocked() const {
  // If there are no more streams and next stream id is greater than the
  // max allowed stream id, then no more streams can be created and it is
  // safe to close the transport.
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::CanCloseTransportLocked "
                            "GetActiveStreamCount="
                         << GetActiveStreamCount()
                         << " PeekNextStreamId=" << PeekNextStreamId()
                         << " GetMaxAllowedStreamId="
                         << GetMaxAllowedStreamId();
  return GetActiveStreamCount() == 0 &&
         PeekNextStreamId() > GetMaxAllowedStreamId();
}

Http2ClientTransport::~Http2ClientTransport() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport Destructor Begin";
  GRPC_DCHECK(stream_list_.empty());
  GRPC_DCHECK(general_party_ == nullptr);
  GRPC_DCHECK(on_receive_settings_ == nullptr);
  memory_owner_.Reset();
  SourceDestructing();
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport Destructor End";
}

void Http2ClientTransport::SpawnAddChannelzData(channelz::DataSink sink) {
  SpawnInfallibleTransportParty(
      "AddData", [self = RefAsSubclass<Http2ClientTransport>(),
                  sink = std::move(sink)]() mutable {
        GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::AddData Promise";
        sink.AddData(
            "Http2ClientTransport",
            channelz::PropertyList()
                .Set("keepalive_time", self->keepalive_time_)
                .Set("keepalive_timeout", self->keepalive_timeout_)
                .Set("ping_timeout", self->ping_timeout_)
                .Set("keepalive_permit_without_calls",
                     self->keepalive_permit_without_calls_)
                .Set("settings", self->settings_.ChannelzProperties())
                .Set("flow_control",
                     self->flow_control_.stats().ChannelzProperties()));
        self->general_party_->ExportToChannelz("Http2ClientTransport Party",
                                               sink);
        GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::AddData End";
        return Empty{};
      });
}

void Http2ClientTransport::AddData(channelz::DataSink sink) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::AddData Begin";

  event_engine_->Run([self = RefAsSubclass<Http2ClientTransport>(),
                      sink = std::move(sink)]() mutable {
    {
      // Apart from CloseTransport, this is the only place where a lock is taken
      // to access general_party_. All other access to general_party_ happens
      // on the general party itself and hence do not race with CloseTransport.
      // TODO(akshitpatel) : [PH2][P4] : Check if a new mutex is needed to
      // protect general_party_. Curently transport_mutex_ can is used in
      // these places:
      // 1. In promises running on the transport party
      // 2. In AddData promise
      // 3. In Orphan function.
      // 4. Stream creation (this will be removed soon).
      // Given that #1 is already serialized (guaranteed by party), #2 is on
      // demand and #3 happens once for the lifetime of the transport while
      // closing the transport, the contention should be minimal.
      MutexLock lock(&self->transport_mutex_);
      if (self->general_party_ == nullptr) {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::AddData general_party_ is "
               "null. Transport is closed.";
        return;
      }
    }

    ExecCtx exec_ctx;
    self->SpawnAddChannelzData(std::move(sink));
  });
}

///////////////////////////////////////////////////////////////////////////////
// Stream Related Operations

RefCountedPtr<Stream> Http2ClientTransport::LookupStream(uint32_t stream_id) {
  MutexLock lock(&transport_mutex_);
  auto it = stream_list_.find(stream_id);
  if (it == stream_list_.end()) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::LookupStream Stream not found stream_id="
        << stream_id;
    return nullptr;
  }
  return it->second;
}

bool Http2ClientTransport::SetOnDone(CallHandler call_handler,
                                     RefCountedPtr<Stream> stream) {
  return call_handler.OnDone(
      [self = RefAsSubclass<Http2ClientTransport>(), stream,
       stream_id = stream->GetStreamId()](bool cancelled) {
        GRPC_HTTP2_CLIENT_DLOG << "PH2: Client call " << self.get()
                               << " id=" << stream_id
                               << " done: cancelled=" << cancelled;
        absl::StatusOr<EnqueueResult> enqueue_result;
        GRPC_HTTP2_CLIENT_DLOG
            << "PH2: Client call " << self.get() << " id=" << stream_id
            << " done: stream=" << stream.get() << " cancelled=" << cancelled;
        if (cancelled) {
          // In most of the cases, EnqueueResetStream would be a no-op as
          // BeginCloseStream would have already enqueued the reset stream.
          // Currently only Aborts from application will actually enqueue
          // the reset stream here.
          enqueue_result = stream->EnqueueResetStream(
              static_cast<uint32_t>(Http2ErrorCode::kCancel));
          GRPC_HTTP2_CLIENT_DLOG
              << "Enqueued ResetStream with error code="
              << static_cast<uint32_t>(Http2ErrorCode::kCancel)
              << " status=" << enqueue_result.status();
        } else {
          enqueue_result = stream->EnqueueHalfClosed();
          GRPC_HTTP2_CLIENT_DLOG << "Enqueued HalfClosed with result="
                                 << enqueue_result.status();
        }

        if (enqueue_result.ok()) {
          GRPC_HTTP2_CLIENT_DLOG
              << "Http2ClientTransport::SetOnDone "
                 "MaybeAddStreamToWritableStreamList for stream= "
              << stream->GetStreamId() << " enqueue_result={became_writable="
              << enqueue_result.value().became_writable << ", priority="
              << static_cast<uint8_t>(enqueue_result.value().priority) << "}";
          GRPC_UNUSED absl::Status status =
              self->MaybeAddStreamToWritableStreamList(stream,
                                                       enqueue_result.value());
        }
      });
}

std::optional<RefCountedPtr<Stream>> Http2ClientTransport::MakeStream(
    CallHandler call_handler) {
  // https://datatracker.ietf.org/doc/html/rfc9113#name-stream-identifiers
  RefCountedPtr<Stream> stream;
  {
    // TODO(akshitpatel) : [PH2][P3] : Remove this mutex once settings is in
    // place.
    MutexLock lock(&transport_mutex_);
    stream = MakeRefCounted<Stream>(
        call_handler, settings_.peer().allow_true_binary_metadata(),
        settings_.acked().allow_true_binary_metadata(), flow_control_);
  }
  const bool on_done_added = SetOnDone(call_handler, stream);
  if (!on_done_added) return std::nullopt;
  return stream;
}

uint32_t Http2ClientTransport::GetMaxAllowedStreamId() const {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport GetMaxAllowedStreamId "
                         << max_allowed_stream_id_;
  return max_allowed_stream_id_;
}

void Http2ClientTransport::SetMaxAllowedStreamId(
    const uint32_t max_allowed_stream_id) {
  const uint32_t old_max_allowed_stream_id = GetMaxAllowedStreamId();
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport SetMaxAllowedStreamId "
                         << " max_allowed_stream_id: " << max_allowed_stream_id
                         << " old_allowed_max_stream_id: "
                         << old_max_allowed_stream_id;
  // RFC9113 : Endpoints MUST NOT increase the value they send in the last
  // stream identifier, since the peers might already have retried unprocessed
  // requests on another connection.
  if (GPR_LIKELY(max_allowed_stream_id <= old_max_allowed_stream_id)) {
    max_allowed_stream_id_ = max_allowed_stream_id;
  } else {
    LOG_IF(ERROR, max_allowed_stream_id > old_max_allowed_stream_id)
        << "Endpoints MUST NOT increase the value they send in the last "
           "stream "
           "identifier";
    GRPC_DCHECK_LE(max_allowed_stream_id, old_max_allowed_stream_id)
        << "Endpoints MUST NOT increase the value they send in the last "
           "stream "
           "identifier";
  }
}

///////////////////////////////////////////////////////////////////////////////
// Call Spine related operations

auto Http2ClientTransport::CallOutboundLoop(CallHandler call_handler,
                                            RefCountedPtr<Stream> stream,
                                            ClientMetadataHandle metadata) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport CallOutboundLoop";
  GRPC_DCHECK(stream != nullptr);

  auto send_message = [self = RefAsSubclass<Http2ClientTransport>(),
                       stream](MessageHandle&& message) mutable {
    return TrySeq(stream->EnqueueMessage(std::move(message)),
                  [self, stream](const EnqueueResult result) mutable {
                    GRPC_HTTP2_CLIENT_DLOG
                        << "Http2ClientTransport CallOutboundLoop "
                           "Enqueued Message";
                    return self->MaybeAddStreamToWritableStreamList(
                        std::move(stream), result);
                  });
  };

  auto send_initial_metadata = [self = RefAsSubclass<Http2ClientTransport>(),
                                stream,
                                metadata = std::move(metadata)]() mutable {
    return TrySeq(
        [stream, metadata = std::move(metadata)]() mutable {
          return stream->EnqueueInitialMetadata(std::move(metadata));
        },
        [self, stream](const EnqueueResult result) mutable {
          GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport CallOutboundLoop "
                                    "Enqueued Initial Metadata";
          return self->MaybeAddStreamToWritableStreamList(std::move(stream),
                                                          result);
        });
  };

  auto send_half_closed = [self = RefAsSubclass<Http2ClientTransport>(),
                           stream]() mutable {
    return TrySeq([stream]() { return stream->EnqueueHalfClosed(); },
                  [self, stream](const EnqueueResult result) mutable {
                    GRPC_HTTP2_CLIENT_DLOG
                        << "Http2ClientTransport CallOutboundLoop "
                           "Enqueued Half Closed";
                    return self->MaybeAddStreamToWritableStreamList(
                        std::move(stream), result);
                  });
  };
  return GRPC_LATENT_SEE_PROMISE(
      "Ph2CallOutboundLoop",
      TrySeq(
          send_initial_metadata(),
          [call_handler, send_message]() {
            // The lock will be released once the promise is constructed from
            // this factory. ForEach will be polled after the lock is
            // released.
            return ForEach(MessagesFrom(call_handler), send_message);
          },
          [self = RefAsSubclass<Http2ClientTransport>(),
           send_half_closed = std::move(send_half_closed)]() mutable {
            return send_half_closed();
          },
          [call_handler]() mutable {
            return Map(call_handler.WasCancelled(), [](bool cancelled) {
              GRPC_HTTP2_CLIENT_DLOG
                  << "Http2ClientTransport PH2CallOutboundLoop End with "
                     "cancelled="
                  << cancelled;
              return (cancelled) ? absl::CancelledError() : absl::OkStatus();
            });
          }));
}

void Http2ClientTransport::StartCall(CallHandler call_handler) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport StartCall Begin";
  call_handler.SpawnGuarded(
      "OutboundLoop",
      TrySeq(call_handler.PullClientInitialMetadata(),
             [self = RefAsSubclass<Http2ClientTransport>(),
              call_handler](ClientMetadataHandle metadata) mutable {
               // For a gRPC Client, we only need to check the
               // MAX_CONCURRENT_STREAMS setting compliance at the time of
               // sending (that is write path). A gRPC Client will never
               // receive a stream initiated by a server, so we dont have to
               // check MAX_CONCURRENT_STREAMS compliance on the Read-Path.
               //
               // TODO(tjagtap) : [PH2][P1] Check for MAX_CONCURRENT_STREAMS
               // sent by peer before making a stream. Decide behaviour if we
               // are crossing this threshold.
               //
               // TODO(tjagtap) : [PH2][P1] : For a server we will have to do
               // this for incoming streams only. If a server receives more
               // streams from a client than is allowed by the clients settings,
               // whether or not we should fail is debatable.
               std::optional<RefCountedPtr<Stream>> stream =
                   self->MakeStream(call_handler);
               return If(
                   stream.has_value(),
                   [self, call_handler, stream,
                    initial_metadata = std::move(metadata)]() mutable {
                     return Map(
                         self->CallOutboundLoop(call_handler, stream.value(),
                                                std::move(initial_metadata)),
                         [](absl::Status status) { return status; });
                   },
                   []() {
                     return absl::InternalError("Failed to make stream");
                   });
             }));
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport StartCall End";
}

}  // namespace http2
}  // namespace grpc_core
