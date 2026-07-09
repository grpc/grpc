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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/flow_control_manager.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/goaway.h"
#include "src/core/ext/transport/chttp2/transport/header_assembler.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_promises.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport.h"
#include "src/core/ext/transport/chttp2/transport/http2_ztrace_collector.h"
#include "src/core/ext/transport/chttp2/transport/keepalive.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
#include "src/core/ext/transport/chttp2/transport/ping_promise.h"
#include "src/core/ext/transport/chttp2/transport/read_context.h"
#include "src/core/ext/transport/chttp2/transport/security_frame.h"
#include "src/core/ext/transport/chttp2/transport/stream.h"
#include "src/core/ext/transport/chttp2/transport/stream_data_queue.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/ext/transport/chttp2/transport/write_cycle.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/status_flag.h"
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

namespace grpc_core {
namespace http2 {

// As a gRPC server never initiates a stream, the last incoming stream id on
// the client side will always be 0.
constexpr uint32_t kLastIncomingStreamIdClient = 0;
const bool kIsClient = true;

using grpc_event_engine::experimental::EventEngine;
using StreamWritabilityUpdate =
    StreamDataQueue<ClientMetadataHandle>::StreamWritabilityUpdate;

// Experimental : This is just the initial skeleton of class
// and it is functions. The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P5] : Update the experimental status of the code when
// http2 rollout is completed.

void Http2ClientTransport::PerformOp(grpc_transport_op* op) {
  // Notes : Refer : src/core/ext/transport/chaotic_good/client_transport.cc
  // Functions : StartConnectivityWatch, StopConnectivityWatch, PerformOp
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::PerformOp Begin";
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
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::PerformOp End";
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
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ReportDisconnectionLocked status="
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

absl::Status Http2ClientTransport::AckPing(uint64_t opaque_data) {
  // It is possible that the PingRatePolicy may decide to not send a ping
  // request (in cases like the number of inflight pings is too high).
  // When this happens, it becomes important to ensure that if a ping ack
  // is received and there is an "important" outstanding ping request, we
  // should retry to send it out now.
  if (ping_manager_->AckPing(opaque_data)) {
    if (ping_manager_->ImportantPingRequested()) {
      return TriggerWriteCycle();
    }
  } else {
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::AckPing Unknown ping "
                              "response received for ping id="
                           << opaque_data;
  }

  return absl::OkStatus();
}

void Http2ClientTransport::Orphan() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::Orphan Begin";
  // Accessing general_party here is not advisable. It may so happen that
  // the party is already freed/may free up any time. The only guarantee here
  // is that the transport is still valid.
  SourceDestructing();
  MaybeSpawnCloseTransport(
      ToHttpOkOrConnError(absl::UnavailableError("Orphaned")));
  Unref();
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::Orphan End";
}

///////////////////////////////////////////////////////////////////////////////
// Processing each type of frame

Http2Status Http2ClientTransport::ProcessIncomingFrame(Http2DataFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-data
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ProcessIncomingFrame(DataFrame) { stream_id="
      << frame.stream_id << ", end_stream:" << frame.end_stream
      << ", payload length=" << frame.payload.Length() << "}";

  ping_manager_->ReceivedDataFrame();

  RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);

  if (frame.payload.Length() > 0) {
    // DATA frames with empty payload are legitimate frames. CHTTP2 and PH2 send
    // empty DATA frames with END_Stream flag set to true.
    ValueOrHttp2Status<chttp2::FlowControlAction> flow_control_action =
        ProcessIncomingDataFrameFlowControl(
            read_context_.GetCurrentFrameHeader(), flow_control_, stream.get());
    if (!flow_control_action.IsOk()) {
      return ValueOrHttp2Status<chttp2::FlowControlAction>::TakeStatus(
          std::move(flow_control_action));
    }
    ActOnFlowControlAction(flow_control_action.value(), stream.get());
  }
  if (stream == nullptr) {
    // TODO(tjagtap) : [PH2][P2] : Implement the correct behaviour later.
    // RFC9113 : If a DATA frame is received whose stream is not in the "open"
    // or "half-closed (local)" state, the recipient MUST respond with a stream
    // error (Section 5.4.2) of type STREAM_CLOSED.
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::ProcessIncomingFrame(DataFrame) { stream_id="
        << frame.stream_id << "} Lookup Failed";
    return Http2Status::Ok();
  }

  Http2Status stream_status = stream->CanStreamReceiveDataFrames();
  if (!stream_status.IsOk()) {
    return stream_status;
  }

  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ProcessIncomingFrame(DataFrame) "
         "AppendNewDataFrame";
  GrpcMessageAssembler& assembler = stream->GetGrpcMessageAssembler();
  Http2Status status =
      assembler.AppendNewDataFrame(frame.payload, frame.end_stream);
  if (!status.IsOk()) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::ProcessIncomingFrame(DataFrame) "
           "AppendNewDataFrame Failed";
    return status;
  }

  // Pass the messages up the stack if it is ready.
  while (true) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::ProcessIncomingFrame(DataFrame) "
           "ExtractMessage";
    ValueOrHttp2Status<MessageHandle> result = assembler.ExtractMessage();
    if (!result.IsOk()) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::ProcessIncomingFrame(DataFrame) "
             "ExtractMessage Failed";
      return ValueOrHttp2Status<MessageHandle>::TakeStatus(std::move(result));
    }
    MessageHandle message = TakeValue(std::move(result));
    if (message != nullptr) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::ProcessIncomingFrame(DataFrame) "
             "SpawnPushMessage ";
      stream->GetCallHandler().SpawnPushMessage(std::move(message));
      continue;
    }
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::ProcessIncomingFrame(DataFrame) While Break";
    break;
  }

  return Http2Status::Ok();
}

template <typename T>
Http2Status Http2ClientTransport::ProcessIncomingMetadata(T&& frame) {
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ProcessIncomingMetadata { stream_id="
      << frame.stream_id << ", end_headers=" << frame.end_headers << " }";
  ping_manager_->ReceivedDataFrame();

  RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
  // State update MUST happen before processing the frame.
  read_context_.UpdateState(frame, /*is_existing_stream=*/(stream != nullptr));

  if (stream == nullptr) {
    // TODO(tjagtap) : [PH2][P3] : Implement this.
    // RFC9113 : The identifier of a newly established stream MUST be
    // numerically greater than all streams that the initiating endpoint has
    // opened or reserved. This governs streams that are opened using a HEADERS
    // frame and streams that are reserved using PUSH_PROMISE. An endpoint that
    // receives an unexpected stream identifier MUST respond with a connection
    // error (Section 5.4.1) of type PROTOCOL_ERROR.
    return read_context_.ParseAndDiscardHeaders(
        std::move(frame.payload), frame.end_headers, Http2Status::Ok(),
        settings_->acked().max_header_list_size());
  }

  Http2Status validation_status = ValidateMetadataFrameState(
      frame, *stream, read_context_, settings_->acked().max_header_list_size());
  if (!validation_status.IsOk()) {
    return validation_status;
  }

  Http2Status append_result =
      read_context_.header_assembler().AppendFrame(frame);
  if (!append_result.IsOk()) {
    // Frame payload is not consumed if AppendFrame returns a non-OK
    // status. We need to process it to keep our in consistent state.
    return read_context_.ParseAndDiscardHeaders(
        std::move(frame.payload), frame.end_headers, std::move(append_result),
        settings_->acked().max_header_list_size());
  }

  Http2Status status = ProcessMetadata(stream);
  if (!status.IsOk()) {
    // Frame payload has been moved to the HeaderAssembler. So calling
    // ParseAndDiscardHeaders with an empty buffer.
    return read_context_.ParseAndDiscardHeaders(
        SliceBuffer(), frame.end_headers, std::move(status),
        settings_->acked().max_header_list_size());
  }

  // Frame payload has either been processed or moved to the HeaderAssembler.
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessIncomingFrame(
    Http2HeaderFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-headers
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ProcessIncomingFrame(HeaderFrame) end_stream="
      << frame.end_stream;
  return ProcessIncomingMetadata(std::forward<Http2HeaderFrame>(frame));
}

Http2Status Http2ClientTransport::ProcessIncomingFrame(
    Http2RstStreamFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-rst_stream
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ProcessIncomingFrame(RstStreamFrame) { "
         "stream_id="
      << frame.stream_id << ", error_code=" << frame.error_code << " }";

  read_context_.OnResetFrameReceived();
  Http2ErrorCode error_code = FrameErrorCodeToHttp2ErrorCode(frame.error_code);
  absl::Status status = absl::Status(ErrorCodeToAbslStatusCode(error_code),
                                     "Reset stream frame received.");
  RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
  if (stream != nullptr) {
    HandleStreamStateChange(*stream, stream->OnResetReceived(status));
  }

  // In case of stream error, we do not want the Read Loop to be broken. Hence
  // returning an ok status.
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessIncomingFrame(
    Http2SettingsFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-settings

  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ProcessIncomingFrame(SettingsFrame) { ack="
      << frame.ack << ", settings length=" << frame.settings.size() << "}";

  if (!frame.ack) {
    read_context_.OnSettingsFrameReceived();
    Http2Status s = settings_->BufferPeerSettings(std::move(frame.settings));
    if (!s.IsOk()) {
      return s;
    }
    absl::Status trigger_write_status = TriggerWriteCycle();
    if (!trigger_write_status.ok()) {
      return ToHttpOkOrConnError(trigger_write_status);
    }
    if (GPR_UNLIKELY(!settings_->IsFirstPeerSettingsApplied())) {
      // Apply the first settings before we read any other frames.
      read_context_.SetPauseReadLoop();
    }
  } else {
    Http2Status status = settings_->OnSettingsAckReceived();
    if (!status.IsOk()) {
      return status;
    }
    read_context_.SetMaxHeaderTableSize(settings_->acked().header_table_size());
    read_context_.header_assembler().MaybeSetAllowTrueBinaryMetadataAcked(
        settings_->acked().allow_true_binary_metadata());
    ActOnFlowControlAction(flow_control_.SetAckedInitialWindow(
                               settings_->acked().initial_window_size()),
                           /*stream=*/nullptr);
  }

  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessIncomingFrame(Http2PingFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-ping
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ProcessIncomingFrame(PingFrame) { ack="
      << frame.ack << ", opaque=" << frame.opaque << " }";
  if (frame.ack) {
    return ToHttpOkOrConnError(AckPing(frame.opaque));
  } else {
    read_context_.OnPingFrameReceived();
    if (test_only_ack_pings_) {
      ping_manager_->AddPendingPingAck(frame.opaque);
      return ToHttpOkOrConnError(TriggerWriteCycle());
    } else {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::ProcessIncomingFrame(PingFrame) "
             "test_only_ack_pings_ is false. Ignoring the ping request.";
    }
  }
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessIncomingFrame(
    Http2GoawayFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-goaway
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ProcessIncomingFrame(GoawayFrame) { "
         "last_stream_id="
      << frame.last_stream_id << ", error_code=" << frame.error_code << "}";
  LOG_IF(ERROR,
         frame.error_code != static_cast<uint32_t>(Http2ErrorCode::kNoError))
      << "Received GOAWAY frame with error code: " << frame.error_code;

  uint32_t last_stream_id = 0;
  absl::Status status(ErrorCodeToAbslStatusCode(
                          FrameErrorCodeToHttp2ErrorCode(frame.error_code)),
                      frame.debug_data.empty()
                          ? absl::string_view("GOAWAY received")
                          : frame.debug_data.as_string_view());
  if (GoawayManager::IsGracefulGoaway(frame)) {
    const uint32_t next_stream_id = PeekNextStreamId();
    last_stream_id = (next_stream_id > 1) ? next_stream_id - 2 : 0;
  } else {
    last_stream_id = frame.last_stream_id;
  }
  SetMaxAllowedStreamId(last_stream_id);

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
    if (!IsSubchannelConnectionScalingEnabled()) {
      status.SetPayload(kKeepaliveThrottlingKey,
                        absl::Cord(std::to_string(throttled_keepalive_time)));
    }
    disconnect_info.keepalive_time =
        Duration::Milliseconds(throttled_keepalive_time);
  }

  // lie: use transient failure from the transport to indicate goaway has been
  // received.
  ReportDisconnection(status, disconnect_info, "got_goaway");
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessIncomingFrame(
    Http2WindowUpdateFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-window_update
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ProcessIncomingFrame(WindowUpdateFrame) { "
         " stream_id="
      << frame.stream_id << ", increment=" << frame.increment << "}";

  RefCountedPtr<Stream> stream = nullptr;
  if (frame.stream_id != 0) {
    stream = LookupStream(frame.stream_id);
  }

  const bool should_trigger_write = ProcessIncomingWindowUpdateFrameFlowControl(
      frame, flow_control_, stream.get());

  if (should_trigger_write) {
    return ToHttpOkOrConnError(TriggerWriteCycle());
  }

  if (stream != nullptr) {
    StreamWritabilityUpdate update =
        stream->UpdateStreamWritability(GetStreamFlowControlTokens(
            stream->GetStreamFlowControl(), settings_->peer()));
    if (update.became_writable) {
      absl::Status status = writable_stream_list_.EnqueueWrapper(
          stream, update.priority, AreTransportFlowControlTokensAvailable());
      if (!status.ok()) {
        return ToHttpOkOrConnError(status);
      }
    }
  }
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessIncomingFrame(
    Http2ContinuationFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-continuation
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ProcessIncomingFrame(ContinuationFrame)";
  return ProcessIncomingMetadata(std::forward<Http2ContinuationFrame>(frame));
}

Http2Status Http2ClientTransport::ProcessIncomingFrame(
    Http2SecurityFrame&& frame) {
  if (settings_->IsSecurityFrameExpected()) {
    security_frame_handler_->ProcessPayload(std::move(frame.payload));
  }
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessIncomingFrame(
    GRPC_UNUSED Http2UnknownFrame&& frame) {
  // RFC9113: Implementations MUST ignore and discard frames of
  // unknown types.
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ProcessIncomingFrame(UnknownFrame) ";
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessIncomingFrame(
    GRPC_UNUSED Http2EmptyFrame&& frame) {
  LOG(DFATAL) << "ParseFramePayload should never return a Http2EmptyFrame";
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessMetadata(
    RefCountedPtr<Stream> stream) {
  HeaderAssembler& assembler = read_context_.header_assembler();
  CallHandler& call = stream->GetCallHandler();

  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::ProcessMetadata";
  if (assembler.IsReady()) {
    ValueOrHttp2Status<ServerMetadataHandle> read_result =
        assembler.ReadMetadata(read_context_.parser(),
                               !read_context_.HeaderHasEndStream(),
                               /*max_header_list_size_soft_limit=*/
                               read_context_.soft_limit(),
                               /*max_header_list_size_hard_limit=*/
                               settings_->acked().max_header_list_size());
    if (read_result.IsOk()) {
      ServerMetadataHandle metadata = TakeValue(std::move(read_result));
      if (read_context_.HeaderHasEndStream()) {
        HandleStreamStateChange(
            *stream, stream->OnTrailingMetadataReceived(std::move(metadata)));
      } else {
        GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::ProcessMetadata "
                                  "SpawnPushServerInitialMetadata";
        metadata->Set(PeerString(), read_context_.peer_string());
        stream->SetInitialMetadataReceived();
        call.SpawnPushServerInitialMetadata(std::move(metadata));
      }
      return Http2Status::Ok();
    }
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::ProcessMetadata Failed";
    return ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>>::TakeStatus(
        std::move(read_result));
  }
  return Http2Status::Ok();
}

///////////////////////////////////////////////////////////////////////////////
// Read Related Promises and Promise Factories
auto Http2ClientTransport::ReadAndProcessOneFrame() {
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::ReadAndProcessOneFrame Factory";
  return AssertResultType<absl::Status>(TrySeq(
      // Fetch the first kFrameHeaderSize bytes of the Frame, these contain
      // the frame header.
      EndpointReadSlice(kFrameHeaderSize),
      // Parse the frame header.
      [this](Slice header_bytes) {
        Http2FrameHeader header = Http2FrameHeader::Parse(header_bytes.begin());
        // Validate the incoming frame as per the current state of the transport
        Http2Status status = read_context_.ValidateHeader(
            /*max_frame_size_setting=*/settings_->acked().max_frame_size(),
            /*current_frame_header=*/header,
            /*last_stream_id=*/GetLastStreamId(),
            /*is_first_settings_processed=*/
            settings_->IsFirstPeerSettingsApplied());

        if (GPR_UNLIKELY(!status.IsOk())) {
          GRPC_DCHECK(status.GetType() ==
                      Http2Status::Http2ErrorType::kConnectionError);
          return HandleError(/*stream=*/nullptr, std::move(status));
        }
        read_context_.SetCurrentFrameHeader(header);
        return absl::OkStatus();
      },
      // Read the payload of the frame.
      [this]() {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::ReadAndProcessOneFrame Read Frame ";
        return AssertResultType<absl::Status>(
            Map(EndpointRead(read_context_.GetCurrentFrameHeader().length),
                [this](absl::StatusOr<SliceBuffer>&& payload) {
                  if (GPR_UNLIKELY(!payload.ok())) {
                    return payload.status();
                  }
                  GRPC_HTTP2_CLIENT_DLOG
                      << "Http2ClientTransport::ReadAndProcessOneFrame "
                         "ParseFramePayload payload length: "
                      << payload.value().Length();
                  ValueOrHttp2Status<Http2Frame> frame =
                      ParseFramePayload(read_context_.GetCurrentFrameHeader(),
                                        TakeValue(std::move(payload)));
                  if (GPR_UNLIKELY(!frame.IsOk())) {
                    return HandleError(
                        LookupStream(
                            read_context_.GetCurrentFrameHeader().stream_id),
                        ValueOrHttp2Status<Http2Frame>::TakeStatus(
                            std::move(frame)));
                  }
                  Http2Status status =
                      ProcessOneIncomingFrame(TakeValue(std::move(frame)));
                  if (GPR_UNLIKELY(!status.IsOk())) {
                    return HandleError(
                        LookupStream(
                            read_context_.GetCurrentFrameHeader().stream_id),
                        std::move(status));
                  }
                  return absl::OkStatus();
                }));
      },
      [this]() -> Poll<absl::Status> {
        Poll<absl::Status> poll_result = read_context_.MaybePauseReadLoop();
        if (poll_result.pending()) {
          TriggerWriteCycleOrHandleError();
        }
        return poll_result;
      }));
}

auto Http2ClientTransport::ReadLoop() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::ReadLoop Factory";
  return AssertResultType<absl::Status>(Loop([this]() {
    return TrySeq(ReadAndProcessOneFrame(), []() -> LoopCtl<absl::Status> {
      GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::ReadLoop Continue";
      return Continue();
    });
  }));
}

///////////////////////////////////////////////////////////////////////////////
// Flow Control for the Transport

auto Http2ClientTransport::FlowControlPeriodicUpdateLoop() {
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::FlowControlPeriodicUpdateLoop Factory";
  return AssertResultType<absl::Status>(
      Loop([this]() {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::FlowControlPeriodicUpdateLoop Loop";
        return TrySeq(
            // TODO(tjagtap) [PH2][P2][BDP] Remove this static sleep when the
            // BDP code is done.
            Sleep(chttp2::kFlowControlPeriodicUpdateTimer),
            [this]() -> Poll<absl::Status> {
              GRPC_HTTP2_CLIENT_DLOG
                  << "Http2ClientTransport::FlowControlPeriodicUpdateLoop "
                     "PeriodicUpdate()";
              chttp2::FlowControlAction action = flow_control_.PeriodicUpdate();
              bool is_action_empty = action == chttp2::FlowControlAction();
              // This may trigger a write cycle
              ActOnFlowControlAction(action, nullptr);
              if (is_action_empty) {
                // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is
                // done. We must continue to do PeriodicUpdate once BDP is in
                // place.
                MutexLock lock(&transport_mutex_);
                if (GetActiveStreamCountLocked() == 0) {
                  AddPeriodicUpdatePromiseWaker();
                  return Pending{};
                }
              }
              return absl::OkStatus();
            },
            []() -> LoopCtl<absl::Status> { return Continue{}; });
      }));
}

// Equivalent to grpc_chttp2_act_on_flowctl_action in chttp2_transport.cc
void Http2ClientTransport::ActOnFlowControlAction(
    const chttp2::FlowControlAction& action, Stream* stream) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::ActOnFlowControlAction"
                         << action.DebugString();
  if (action.send_stream_update() != kNoActionNeeded) {
    if (GPR_LIKELY(stream != nullptr)) {
      GRPC_DCHECK_GT(stream->GetStreamId(), 0u);
      if (stream->CanSendWindowUpdateFrames()) {
        flow_control_.AddStreamToWindowUpdateList(stream->GetStreamId());
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::ActOnFlowControlAction "
               "added stream "
            << stream->GetStreamId() << " to window_update_list_";
      }
    } else {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::ActOnFlowControlAction stream is null";
    }
  }

  ActOnFlowControlActionSettings(action, settings_->mutable_local());

  if (action.AnyUpdateImmediately()) {
    // Prioritize sending flow control updates over reading data. If we
    // continue reading while urgent flow control updates are pending, we might
    // exhaust the flow control window. This prevents us from sending window
    // updates to the peer, causing the peer to block unnecessarily while
    // waiting for flow control tokens.
    read_context_.SetPauseReadLoop();
    if (!TriggerWriteCycleOrHandleError()) {
      return;
    }

    GRPC_HTTP2_CLIENT_DLOG << "Update Immediately : "
                           << action.ImmediateUpdateReasons();
  }
}

absl::Status Http2ClientTransport::UpdateAllStreamsWritability() {
  MutexLock lock(&transport_mutex_);
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::UpdateAllStreamsWritability total streams: "
      << stream_list_.size();
  // This loop iterates over all active streams. For each stream this would
  // internally take a stream specific lock and update the stream writability.
  // This is not optimal but should be fine as this function is only called when
  // initial window size is increased which in theory should not be very
  // frequent.
  for (const auto& [stream_id, stream] : stream_list_) {
    StreamWritabilityUpdate update =
        stream->UpdateStreamWritability(GetStreamFlowControlTokens(
            stream->GetStreamFlowControl(), settings_->peer()));
    absl::Status status = MaybeAddStreamToWritableStreamList(stream, update);
    if (GPR_UNLIKELY(!status.ok())) {
      GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::"
                                "UpdateAllStreamsWritability failed for stream "
                             << stream_id << " with status " << status;
      return status;
    }
  }

  return absl::OkStatus();
}

///////////////////////////////////////////////////////////////////////////////
// Write Related Promises and Promise Factories
auto Http2ClientTransport::EndpointWrite(SliceBuffer&& output_buf) {
  size_t output_buf_length = output_buf.Length();
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::EndpointWrite output_buf: "
                         << output_buf_length;

  transport_write_context_.GetWriteCycle().BeginWrite(output_buf_length);
  return Map(
      endpoint_.Write(std::forward<SliceBuffer>(output_buf),
                      TransportWriteContext::GetWriteArgs(settings_->peer())),
      [this](absl::Status status) {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::EndpointWrite complete with status = "
            << status;
        transport_write_context_.GetWriteCycle().EndWrite(status.ok());
        return status;
      });
}

absl::Status Http2ClientTransport::PrepareControlFrames() {
  FrameSender frame_sender =
      transport_write_context_.GetWriteCycle().GetFrameSender();
  if (transport_write_context_.IsFirstWrite()) {
    // RFC9113: That is, the connection preface starts with the string
    // "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n". This connection preface string will
    // be sent as part of the first write cycle. This sequence MUST be followed
    // by a SETTINGS frame, which MAY be empty.
    settings_->MaybeGetSettingsAndSettingsAckFrames(flow_control_,
                                                    frame_sender);
    // TODO(tjagtap) [PH2][P2][Server] : This will be opposite for server. We
    // must read before we write for the server. So the ReadLoop will be Spawned
    // just after the constructor, and the write loop should be spawned only
    // after the first SETTINGS frame is completely received.
    //
    // Because the client is expected to write before it reads, we spawn the
    // ReadLoop of the client only after the first write is queued.
    SpawnGuardedTransportParty("ReadLoop", UntilTransportClosed(ReadLoop()));
  }

  // Order of Control Frames is important.
  // 1. GOAWAY - This is first because if this is the final GoAway, then we may
  //             not need to send anything else to the peer.
  // 2. SETTINGS and SETTINGS ACK
  // 3. PING and PING acks.
  // 4. WINDOW_UPDATE
  // 5. Custom gRPC security frame

  goaway_manager_.MaybeGetSerializedGoawayFrame(frame_sender);
  ApplySettingsResult apply_settings_result;

  const uint32_t old_initial_window_size =
      settings_->peer().initial_window_size();
  const http2::Http2ErrorCode apply_status =
      settings_->MaybeReportAndApplyBufferedPeerSettings(event_engine_.get(),
                                                         apply_settings_result);
  if (apply_status == http2::Http2ErrorCode::kNoError) {
    const uint32_t new_initial_window_size =
        settings_->peer().initial_window_size();
    if (new_initial_window_size > old_initial_window_size) {
      // TODO(akshitpatel) [PH2][P5] : Currently, if calling
      // UpdateAllStreamsWritability() makes one or more streams writable. Once
      // a stream is writable, it is enqueued to the writable stream list.
      // However, these streams are not written out until the next write cycle.
      // Might be worth considering to write out these streams immediately.
      settings_->IncrementInitialWindowSizeIncreaseCount();
      absl::Status status = UpdateAllStreamsWritability();
      if (GPR_UNLIKELY(!status.ok())) {
        return status;
      }
    }
  }

  if (apply_settings_result.should_spawn_security_frame_loop) {
    const SecurityFrameHandler::EndpointExtensionState state =
        security_frame_handler_->Initialize(event_engine_);
    if (state.is_set) {
      SpawnInfallibleTransportParty("SecurityFrameLoop",
                                    UntilTransportClosed(SecurityFrameLoop()));
    }
  }

  if (!goaway_manager_.IsImmediateGoAway() &&
      apply_status == http2::Http2ErrorCode::kNoError) {
    EnforceLatestIncomingSettings();
    settings_->MaybeGetSettingsAndSettingsAckFrames(flow_control_,
                                                    frame_sender);
    MaybeSpawnDelayedPing(ping_manager_->MaybeGetSerializedPingFrames(
        frame_sender, NextAllowedPingInterval()));
    MaybeGetWindowUpdateFrames(frame_sender);
    security_frame_handler_->MaybeAppendSecurityFrame(frame_sender);
  }

  if (apply_status != http2::Http2ErrorCode::kNoError) {
    return HandleError(/*stream=*/nullptr,
                       Http2Status::Http2ConnectionError(
                           apply_status, "Failed to apply incoming settings"));
  }

  return absl::OkStatus();
}

auto Http2ClientTransport::MaybeWriteUrgentFrames() {
  return AssertResultType<absl::Status>(If(
      transport_write_context_.GetWriteCycle().CanSerializeUrgentFrames(),
      [this]() mutable {
        WriteCycle& write_cycle = transport_write_context_.GetWriteCycle();
        const uint64_t buffer_length = write_cycle.GetUrgentFrameCount();
        ztrace_collector_->Append(PromiseEndpointWriteTrace{buffer_length});
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::MaybeWriteUrgentFrames frame count: "
            << buffer_length;
        return EndpointWrite(write_cycle.SerializeUrgentFrames(
            WriteCycle::SerializeStats{should_reset_ping_clock_}));
      },
      []() { return absl::OkStatus(); }));
}

void Http2ClientTransport::NotifyFramesWriteDone() {
  // Notify Control modules that we have sent the frames.
  // All notifications are expected to be synchronous.
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::NotifyFramesWriteDone";
  read_context_.ResumeReadLoopIfPaused();
  MaybeSpawnPingTimeout(ping_manager_->NotifyPingSent());
  goaway_manager_.NotifyGoawaySent();
  MaybeSpawnWaitForSettingsTimeout();
}

void Http2ClientTransport::NotifyUrgentFramesWriteDone() {}

auto Http2ClientTransport::SerializeAndWrite() {
  return AssertResultType<absl::Status>(If(
      transport_write_context_.GetWriteCycle().CanSerializeRegularFrames(),
      [this]() mutable {
        WriteCycle& write_cycle = transport_write_context_.GetWriteCycle();
        const uint64_t frame_count = write_cycle.GetRegularFrameCount();
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::SerializeAndWrite frame count: "
            << frame_count;
        ztrace_collector_->Append(PromiseEndpointWriteTrace{frame_count});
        return EndpointWrite(write_cycle.SerializeRegularFrames(
            WriteCycle::SerializeStats{should_reset_ping_clock_}));
      },
      []() { return absl::OkStatus(); }));
}

absl::Status Http2ClientTransport::DequeueStreamFrames(
    RefCountedPtr<Stream> stream, WriteCycle& write_cycle) {
  // write_bytes_remaining_ is passed as an upper bound on the max
  // number of tokens that can be dequeued to prevent dequeuing huge
  // data frames when write_bytes_remaining_ is very low. As the
  // available transport tokens can only range from 0 to 2^31 - 1,
  // we are clamping the write_bytes_remaining_ to that range.
  FrameSender frame_sender = write_cycle.GetFrameSender();
  const uint32_t tokens = GetMaxPermittedDequeue(
      flow_control_, stream->GetStreamFlowControl(),
      write_cycle.GetWriteBytesRemaining(), settings_->peer());
  const uint32_t stream_flow_control_tokens =
      static_cast<uint32_t>(GetStreamFlowControlTokens(
          stream->GetStreamFlowControl(), settings_->peer()));
  stream->GetStreamFlowControl().ReportIfStalled(
      /*is_client=*/kIsClient, stream->GetStreamId(), settings_->peer());
  StreamDataQueue<ClientMetadataHandle>::DequeueResult result =
      stream->DequeueFrames(tokens, stream_flow_control_tokens,
                            settings_->peer().max_frame_size(), encoder_,
                            frame_sender);
  ProcessOutgoingDataFrameFlowControl(stream->GetStreamFlowControl(),
                                      result.flow_control_tokens_consumed);
  if (result.is_writable) {
    // Stream is still writable. Enqueue it back to the writable
    // stream list.
    absl::Status status = writable_stream_list_.EnqueueWrapper(
        stream, result.priority, AreTransportFlowControlTokensAvailable());

    if (GPR_UNLIKELY(!status.ok())) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::DequeueStreamFrames Failed to "
             "enqueue stream "
          << stream->GetStreamId() << " with status: " << status;
      // Close transport if we fail to enqueue stream.
      return HandleError(/*stream=*/nullptr, ToHttpOkOrConnError(status));
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
  if (result.IsInitialMetadataDequeued()) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::DequeueStreamFrames InitialMetadataDequeued "
           "stream_id = "
        << stream->GetStreamId();
    stream->OnInitialMetadataSent();
    // After this point, initial metadata is guaranteed to be sent out.
    AddToStreamList(stream);
  }

  if (result.IsHalfCloseDequeued()) {
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::DequeueStreamFrames "
                              "HalfCloseDequeued stream_id = "
                           << stream->GetStreamId();
    HandleStreamStateChange(*stream, stream->OnHalfCloseSent());
  }
  if (result.IsResetStreamDequeued()) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::DequeueStreamFrames ResetStreamDequeued "
           "stream_id = "
        << stream->GetStreamId();
    HandleStreamStateChange(*stream, stream->OnResetSent());
  }

  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::DequeueStreamFrames "
                            "After dequeue: "
                         << write_cycle.DebugString()
                         << " stream_id = " << stream->GetStreamId()
                         << " is_writable = " << result.is_writable
                         << " stream_priority = "
                         << static_cast<uint8_t>(result.priority);
  return absl::OkStatus();
}

// This MultiplexerLoop promise is responsible for Multiplexing multiple gRPC
// Requests (HTTP2 Streams) and writing them into one common endpoint.
auto Http2ClientTransport::MultiplexerLoop() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::MultiplexerLoop Factory";
  return AssertResultType<absl::Status>(Loop([this]() {
    return TrySeq(
        Map(writable_stream_list_.WaitForReady(
                AreTransportFlowControlTokensAvailable()),
            [this](absl::StatusOr<Empty> status) -> absl::Status {
              if (GPR_UNLIKELY(!status.ok())) {
                return status.status();
              }
              transport_write_context_.StartWriteCycle();
              GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::MultiplexerLoop "
                                        "Created WriteCycle: "
                                     << transport_write_context_.DebugString();
              return PrepareControlFrames();
            }),
        [this] {
          return Map(MaybeWriteUrgentFrames(), [this](absl::Status status) {
            if (GPR_UNLIKELY(!status.ok())) {
              return status;
            }
            NotifyUrgentFramesWriteDone();
            WriteCycle& write_cycle = transport_write_context_.GetWriteCycle();
            // Drain all the writable streams till we have written
            // max_write_size_ bytes of data or there is no more data to send.
            // In some cases, we may write more than max_write_size_ bytes(like
            // writing metadata).
            while (write_cycle.GetWriteBytesRemaining() > 0) {
              std::optional<RefCountedPtr<Stream>> optional_stream =
                  writable_stream_list_.ImmediateNext(
                      AreTransportFlowControlTokensAvailable());
              if (!optional_stream.has_value()) {
                GRPC_HTTP2_CLIENT_DLOG
                    << "Http2ClientTransport::MultiplexerLoop "
                       "No writable streams available ";
                break;
              }
              RefCountedPtr<Stream> stream = std::move(optional_stream.value());
              GRPC_HTTP2_CLIENT_DLOG
                  << "Http2ClientTransport::MultiplexerLoop "
                     "Next writable stream id = "
                  << stream->GetStreamId()
                  << " is_closed_for_writes = " << stream->IsClosedForWrites();

              if (stream->GetStreamId() == kInvalidStreamId) {
                GRPC_DCHECK(stream->IsStreamIdle());
                // TODO(akshitpatel) : [PH2][P5] : We will waste a stream id in
                // the rare scenario where the stream is aborted before it can
                // be written to. This is a possible area to optimize in future.
                absl::Status status = InitializeStream(*stream);
                if (!status.ok()) {
                  GRPC_HTTP2_CLIENT_DLOG
                      << "Http2ClientTransport::MultiplexerLoop "
                         "Failed to assign stream id and add to stream list for"
                         " stream: "
                      << stream.get() << " closing this stream.";
                  HandleStreamStateChange(*stream, stream->ForceClose(status));
                  continue;
                }
              }

              if (GPR_LIKELY(!stream->IsClosedForWrites())) {
                absl::Status status = DequeueStreamFrames(
                    std::move(stream),
                    transport_write_context_.GetWriteCycle());
                if (GPR_UNLIKELY(!status.ok())) {
                  GRPC_HTTP2_CLIENT_DLOG
                      << "Http2ClientTransport::MultiplexerLoop "
                         "Failed to dequeue stream frames with status: "
                      << status;
                  return status;
                }
              }
            }

            GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::MultiplexerLoop "
                                      "After draining all writable streams "
                                   << write_cycle.DebugString();

            return absl::OkStatus();
          });
        },
        [this]() {
          return Map(SerializeAndWrite(), [this](absl::Status status) {
            if (GPR_UNLIKELY(!status.ok())) {
              return status;
            }
            NotifyFramesWriteDone();
            return absl::OkStatus();
          });
        },
        [this]() -> LoopCtl<absl::Status> {
          if (should_reset_ping_clock_) {
            GRPC_HTTP2_CLIENT_DLOG
                << "Http2ClientTransport::MultiplexerLoop ResetPingClock";
            ping_manager_->ResetPingClock(/*is_client=*/kIsClient);
            should_reset_ping_clock_ = false;
          }
          transport_write_context_.EndWriteCycle();
          return Continue();
        });
  }));
}

absl::Status Http2ClientTransport::InitializeStream(Stream& stream) {
  absl::StatusOr<uint32_t> next_stream_id = NextStreamId();
  if (!next_stream_id.ok()) {
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::InitializeStream "
                              "Failed to get next stream id for stream: "
                           << &stream;
    return std::move(next_stream_id).status();
  }
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::InitializeStream "
                            "Assigned stream id: "
                         << next_stream_id.value() << " to stream: " << &stream
                         << ", allow_true_binary_metadata:"
                         << settings_->peer().allow_true_binary_metadata();
  stream.InitializeClientStream(next_stream_id.value(),
                                settings_->peer().allow_true_binary_metadata());
  return absl::OkStatus();
}

void Http2ClientTransport::AddToStreamList(RefCountedPtr<Stream> stream) {
  bool should_wake_periodic_updates = false;
  {
    MutexLock lock(&transport_mutex_);
    GRPC_DCHECK(stream != nullptr) << "stream is null";
    GRPC_DCHECK_GT(stream->GetStreamId(), 0u) << "stream id is invalid";
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::AddToStreamList for stream id: "
        << stream->GetStreamId();
    const uint32_t stream_id = stream->GetStreamId();
    stream_list_.emplace(stream_id, std::move(stream));
    // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
    if (GetActiveStreamCountLocked() == 1) {
      should_wake_periodic_updates = true;
    }
  }
  // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
  if (should_wake_periodic_updates) {
    // Release the lock before you wake up another promise on the party.
    WakeupPeriodicUpdatePromise();
  }
}

///////////////////////////////////////////////////////////////////////////////
// Settings and Window Update Management

void Http2ClientTransport::EnforceLatestIncomingSettings() {
  encoder_.SetMaxTableSize(settings_->peer().header_table_size());
}

auto Http2ClientTransport::WaitForSettingsTimeoutOnDone() {
  return [self = RefAsSubclass<Http2ClientTransport>()](absl::Status status) {
    if (!status.ok()) {
      GRPC_UNUSED absl::Status result = self->HandleError(
          /*stream=*/nullptr, Http2Status::Http2ConnectionError(
                                  Http2ErrorCode::kProtocolError,
                                  std::string(RFC9113::kSettingsTimeout)));
    }
  };
}

void Http2ClientTransport::MaybeSpawnWaitForSettingsTimeout() {
  if (settings_->ShouldSpawnWaitForSettingsTimeout()) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::MaybeSpawnWaitForSettingsTimeout Spawning";
    SpawnWithOnDoneTransportParty("WaitForSettingsTimeout",
                                  settings_->WaitForSettingsTimeout(),
                                  WaitForSettingsTimeoutOnDone());
  }
}

void Http2ClientTransport::MaybeGetWindowUpdateFrames(
    FrameSender& frame_sender) {
  frame_sender.ReserveRegularFrames(flow_control_.window_update_list_size() +
                                    1);
  MaybeAddTransportWindowUpdateFrame(flow_control_, frame_sender);
  for (const uint32_t stream_id : flow_control_.DrainWindowUpdateList()) {
    RefCountedPtr<Stream> stream = LookupStream(stream_id);
    if (stream != nullptr) {
      MaybeAddStreamWindowUpdateFrame(*stream, frame_sender);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Constructor Destructor

Http2ClientTransport::Http2ClientTransport(
    PromiseEndpoint endpoint, const ChannelArgs& channel_args,
    std::shared_ptr<EventEngine> event_engine,
    absl::AnyInvocable<void(absl::StatusOr<uint32_t>)> on_receive_settings)
    : channelz::DataSource(http2::CreateChannelzSocketNode(
          endpoint.GetEventEngineEndpoint(), channel_args)),
      event_engine_(std::move(event_engine)),
      endpoint_(std::move(endpoint)),
      settings_(MakeRefCounted<SettingsPromiseManager>(
          kIsClient, std::move(on_receive_settings))),
      next_stream_id_(/*Initial Stream ID*/ 1),
      should_reset_ping_clock_(false),
      read_context_(MaxNewStreamsPerRead(channel_args), endpoint_, kIsClient,
                    GetMaxSecurityFrameSize(channel_args)),
      transport_write_context_(kIsClient),
      ping_manager_(std::nullopt),
      keepalive_manager_(std::nullopt),
      goaway_manager_(GoawayInterfaceImpl::Make(this)),
      memory_owner_(channel_args.GetObject<ResourceQuota>()
                        ->memory_quota()
                        ->CreateMemoryOwner()),
      flow_control_(
          /*peer_name=*/read_context_.peer_string().as_string_view(),
          channel_args.GetBool(GRPC_ARG_HTTP2_BDP_PROBE).value_or(true),
          &memory_owner_),
      security_frame_handler_(MakeRefCounted<SecurityFrameHandler>()),
      ztrace_collector_(std::make_shared<PromiseHttp2ZTraceCollector>()) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::Http2ClientTransport Begin";
  // Initialize the general party and write party.
  RefCountedPtr<Arena> party_arena = SimpleArenaAllocator(0)->MakeArena();
  party_arena->SetContext<EventEngine>(event_engine_.get());
  general_party_ = Party::Make(std::move(party_arena));

  InitLocalSettings(settings_->mutable_local(), /*is_client=*/kIsClient);
  TransportChannelArgs args;
  ReadChannelArgs(channel_args, args);

  ping_manager_.emplace(channel_args, args.ping_timeout,
                        PingSystemInterfaceImpl::Make(this), event_engine_);

  // The keepalive loop is only spawned if the keepalive time is not infinity.
  keepalive_manager_.emplace(
      KeepAliveInterfaceImpl::Make(this),
      ((args.keepalive_timeout < args.ping_timeout) ? args.keepalive_timeout
                                                    : Duration::Infinity()),
      args.keepalive_time);

  GRPC_DCHECK(ping_manager_.has_value());
  GRPC_DCHECK(keepalive_manager_.has_value());
  SourceConstructed();
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::Http2ClientTransport End";
}

void Http2ClientTransport::SpawnTransportLoops() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::SpawnTransportLoops Begin";
  MaybeSpawnKeepaliveLoop();
  SpawnGuardedTransportParty(
      "FlowControlPeriodicUpdateLoop",
      UntilTransportClosed(FlowControlPeriodicUpdateLoop()));

  if (!TriggerWriteCycleOrHandleError()) {
    return;
  }
  // For Client, write happens before read. So MultiplexerLoop is spawned first.
  // ReadLoop is spawned after the first write.
  // For Server, read happens before write. So ReadLoop is spawned first.
  SpawnGuardedTransportParty("MultiplexerLoop",
                             UntilTransportClosed(MultiplexerLoop()));
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::SpawnTransportLoops End";
}

void Http2ClientTransport::ReadChannelArgs(const ChannelArgs& channel_args,
                                           TransportChannelArgs& args) {
  http2::ReadChannelArgs(channel_args, args, settings_->mutable_local(),
                         flow_control_,
                         /*is_client=*/kIsClient);

  // Assign the channel args to the member variables.
  keepalive_time_ = args.keepalive_time;
  read_context_.set_soft_limit(args.max_header_list_size_soft_limit);
  keepalive_permit_without_calls_ = args.keepalive_permit_without_calls;
  test_only_ack_pings_ = args.test_only_ack_pings;

  if (args.initial_sequence_number > 0) {
    next_stream_id_ = args.initial_sequence_number;
  }

  settings_->SetSettingsTimeout(args.settings_timeout);
  if (args.max_usable_hpack_table_size >= 0) {
    encoder_.SetMaxUsableSize(args.max_usable_hpack_table_size);
  }
}

absl::Status Http2ClientTransport::HandleError(RefCountedPtr<Stream> stream,
                                               Http2Status status,
                                               DebugLocation whence) {
  Http2Status::Http2ErrorType error_type = status.GetType();
  GRPC_DCHECK(error_type != Http2Status::Http2ErrorType::kOk);

  if (error_type == Http2Status::Http2ErrorType::kStreamError) {
    GRPC_HTTP2_CLIENT_ERROR_DLOG
        << "Http2ClientTransport::HandleError Stream Error:"
        << status.DebugString();
    GRPC_DCHECK(stream != nullptr);
    // Passing a cancelled server metadata handle to propagate the error
    // to the upper layers.
    BeginCloseStream(
        std::move(stream),
        Http2ErrorCodeToFrameErrorCode(status.GetStreamErrorCode()),
        status.GetAbslStreamError(), whence);
    return absl::OkStatus();
  } else if (error_type == Http2Status::Http2ErrorType::kConnectionError) {
    GRPC_HTTP2_CLIENT_ERROR_DLOG
        << "Http2ClientTransport::HandleError Connection Error:"
        << status.DebugString();
    absl::Status absl_status = status.GetAbslConnectionError();
    MaybeSpawnCloseTransport(std::move(status), whence);
    return absl_status;
  }
  GPR_UNREACHABLE_CODE(return absl::InternalError("Invalid error type"));
}

void Http2ClientTransport::HandleStreamStateChange(Stream& stream,
                                                   StreamStateChange change) {
  if (change.reads_became_closed) {
    // If a stream is closing for reads and was actively waiting for a
    // continuation frame, parse the buffered HEADER/CONTINUATION frames
    if (read_context_.IsWaitingForContinuationFrame() &&
        read_context_.GetStreamId() == stream.GetStreamId()) {
      Http2Status result = read_context_.ParseAndDiscardHeaders(
          SliceBuffer(), /*is_end_headers=*/false,
          /*original_status=*/Http2Status::Ok(),
          settings_->acked().max_header_list_size());
      if (GPR_UNLIKELY(result.GetType() ==
                       Http2Status::Http2ErrorType::kConnectionError)) {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::HandleStreamStateChange (DiscardHeaders) "
               "for "
               "stream id: "
            << stream.GetStreamId()
            << " failed to partially process header: " << result.DebugString();
        GRPC_UNUSED absl::Status unused =
            HandleError(/*stream=*/nullptr, std::move(result));
        return;
      }
    }
  }
  if (change.stream_became_closed) {
    CleanupStream(stream);
  }
}

void Http2ClientTransport::CleanupStream(Stream& stream) {
  MutexLock lock(&transport_mutex_);
  stream_list_.erase(stream.GetStreamId());
}

void Http2ClientTransport::BeginCloseStream(
    RefCountedPtr<Stream> stream, uint32_t reset_stream_error_code,
    absl::Status trailing_metadata_status, DebugLocation whence) {
  GRPC_DCHECK(!trailing_metadata_status.ok());
  if (stream == nullptr) {
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::BeginCloseStream stream "
                              "is null reset_stream_error_code="
                           << reset_stream_error_code;
    return;
  }

  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport::BeginCloseStream for stream id: "
      << stream->GetStreamId() << " error_code=" << reset_stream_error_code
      << " status=" << trailing_metadata_status << " location=" << whence.file()
      << ":" << whence.line();

  // Enqueue RST_STREAM.
  absl::StatusOr<StreamWritabilityUpdate> enqueue_result =
      stream->EnqueueResetStream(reset_stream_error_code);
  GRPC_HTTP2_CLIENT_DLOG << "Enqueued ResetStream with error code="
                         << reset_stream_error_code
                         << " status=" << enqueue_result.status();
  if (enqueue_result.ok()) {
    GRPC_UNUSED absl::Status status =
        MaybeAddStreamToWritableStreamList(stream, enqueue_result.value());
  }
  // Close reads immediately. Writes will be closed by the write loop after
  // the RST_STREAM frame is written.
  HandleStreamStateChange(*stream,
                          stream->OnInitiateReset(trailing_metadata_status));
  read_context_.OnResetFrameEnqueued(reset_stream_error_code);
}

void Http2ClientTransport::CloseTransport() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::CloseTransport";

  transport_closed_latch_.Set();
  settings_->HandleTransportShutdown(event_engine_.get());

  // This is the only place where the general_party_ is reset.
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
  ReportDisconnectionLocked(
      http2_status.GetAbslConnectionError(), {},
      absl::StrCat("Transport closed: ", http2_status.DebugString()).c_str());
  lock.Release();

  SpawnInfallibleTransportParty(
      "CloseTransport", [self = RefAsSubclass<Http2ClientTransport>(),
                         stream_list = std::move(stream_list),
                         http2_status = std::move(http2_status)]() mutable {
        self->security_frame_handler_->OnTransportClosed();
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::MaybeSpawnCloseTransport "
               "Cleaning up call stacks";
        // Clean up the call stacks for all active streams.
        for (const auto& pair : stream_list) {
          // There is no merit in transitioning the stream to
          // closed state here as the subsequent lookups would
          // fail. Also, as this is running on the transport
          // party, there would not be concurrent access to the stream.
          RefCountedPtr<Stream> stream = pair.second;
          self->BeginCloseStream(std::move(stream),
                                 Http2ErrorCodeToFrameErrorCode(
                                     http2_status.GetConnectionErrorCode()),
                                 http2_status.GetAbslConnectionError());
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

Http2ClientTransport::~Http2ClientTransport() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::~Http2ClientTransport Begin";
  GRPC_DCHECK(stream_list_.empty());
  GRPC_DCHECK(general_party_ == nullptr);
  memory_owner_.Reset();
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::~Http2ClientTransport End";
}

void Http2ClientTransport::SpawnAddChannelzData(RefCountedPtr<Party> party,
                                                channelz::DataSink sink) {
  SpawnInfallible(
      std::move(party), "AddData",
      [self = RefAsSubclass<Http2ClientTransport>(),
       sink = std::move(sink)]() mutable {
        GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::SpawnAddChannelzData";
        sink.AddData(
            "Http2ClientTransport",
            channelz::PropertyList()
                .Set("keepalive_time", self->keepalive_time_)
                .Set("keepalive_permit_without_calls",
                     self->keepalive_permit_without_calls_)
                .Set("max_requests_per_read",
                     self->read_context_.max_new_streams_per_read_cycle())
                .Set("settings", self->settings_->ChannelzProperties())
                .Set("flow_control",
                     self->flow_control_.stats().ChannelzProperties()));
        self->general_party_->ExportToChannelz("Http2ClientTransport Party",
                                               sink);
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::SpawnAddChannelzData End";
        return Empty{};
      });
}

void Http2ClientTransport::AddData(channelz::DataSink sink) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::AddData Begin";

  event_engine_->Run([self = RefAsSubclass<Http2ClientTransport>(),
                      sink = std::move(sink)]() mutable {
    RefCountedPtr<Party> party = nullptr;
    {
      MutexLock lock(&self->transport_mutex_);
      if (GPR_LIKELY(!self->is_transport_closed_)) {
        GRPC_DCHECK(self->general_party_ != nullptr);
        party = self->general_party_;
      } else {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::AddData Transport is closed.";
      }
    }

    ExecCtx exec_ctx;
    if (party != nullptr) {
      self->SpawnAddChannelzData(std::move(party), std::move(sink));
    }
    self.reset();  // Cleanup with exec_ctx in scope
  });
}

RefCountedPtr<channelz::SocketNode> Http2ClientTransport::GetSocketNode()
    const {
  const channelz::BaseNode* node = channelz::DataSource::channelz_node();
  if (node == nullptr) {
    return nullptr;
  }
  return const_cast<channelz::BaseNode*>(node)
      ->RefAsSubclass<channelz::SocketNode>();
}

///////////////////////////////////////////////////////////////////////////////
// Stream Related Operations

absl::StatusOr<uint32_t> Http2ClientTransport::NextStreamId() {
  if (next_stream_id_ > GetMaxAllowedStreamId()) {
    // TODO(tjagtap) : [PH2][P2] : Handle case if transport runs out of stream
    // ids. Similar check is there in the same function. Check what to do.
    // RFC9113 : Stream identifiers cannot be reused. Long-lived connections
    // can result in an endpoint exhausting the available range of stream
    // identifiers. A client that is unable to establish a new stream
    // identifier can establish a new connection for new streams. A server
    // that is unable to establish a new stream identifier can send a GOAWAY
    // frame so that the client is forced to open a new connection for new
    // streams.
    return absl::ResourceExhaustedError("No more stream ids available");
  }
  // TODO(akshitpatel) : [PH2][P3] : There is a channel arg to delay
  // starting new streams instead of failing them. This needs to be
  // implemented.
  {
    // TODO(tjagtap) : [PH2][P1] : For a server we will have to do
    // this for incoming streams only. If a server receives more
    // streams from a client than is allowed by the clients settings,
    // whether or not we should fail is debatable.
    MutexLock lock(&transport_mutex_);
    if (GetActiveStreamCountLocked() >=
        settings_->peer().max_concurrent_streams()) {
      return absl::ResourceExhaustedError("Reached max concurrent streams");
    }
  }

  // RFC9113 : Streams initiated by a client MUST use odd-numbered stream
  // identifiers.
  uint32_t new_stream_id = std::exchange(next_stream_id_, next_stream_id_ + 2);
  if (GPR_UNLIKELY(next_stream_id_ > GetMaxAllowedStreamId())) {
    ReportDisconnection(
        absl::ResourceExhaustedError("Transport Stream IDs exhausted"),
        {},  // TODO(tjagtap) : [PH2][P2] : Report better disconnect info.
        "no_more_stream_ids");
  }
  return new_stream_id;
}

absl::Status Http2ClientTransport::MaybeAddStreamToWritableStreamList(
    RefCountedPtr<Stream> stream,
    const StreamDataQueue<ClientMetadataHandle>::StreamWritabilityUpdate
        result) {
  if (result.became_writable) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::MaybeAddStreamToWritableStreamList Stream "
           "id: "
        << stream->GetStreamId() << " became writable";
    // TODO(akshitpatel) [PH2][P4][Perf]: Might be worth exploring if this
    // function should take a raw stream ptr and take a ref here.
    absl::Status status =
        writable_stream_list_.Enqueue(std::move(stream), result.priority);
    if (!status.ok()) {
      return HandleError(
          /*stream=*/nullptr,
          Http2Status::Http2ConnectionError(
              Http2ErrorCode::kRefusedStream,
              std::string(GrpcErrors::kFailedToEnqueueStream)));
    }
  }
  return absl::OkStatus();
}

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
  return call_handler.OnDone([self = RefAsSubclass<Http2ClientTransport>(),
                              stream =
                                  std::move(stream)](bool cancelled) mutable {
    GRPC_HTTP2_CLIENT_DLOG << "PH2: Client call " << self.get()
                           << " id=" << stream->GetStreamId()
                           << " done: cancelled=" << cancelled;
    absl::StatusOr<StreamWritabilityUpdate> enqueue_result;
    GRPC_HTTP2_CLIENT_DLOG << "PH2: Client call " << self.get()
                           << " id=" << stream->GetStreamId()
                           << " done: stream=" << stream.get()
                           << " cancelled=" << cancelled;

    // If the stream is already closed for writes, then we don't need to
    // enqueue the reset stream or the half closed frame.
    if (stream->IsClosedForWrites()) {
      GRPC_HTTP2_CLIENT_DLOG << "PH2: Client call " << self.get()
                             << " id=" << stream->GetStreamId()
                             << " done: stream already closed for writes";
      return;
    }

    if (cancelled) {
      // In most of the cases, EnqueueResetStream would be a no-op as
      // BeginCloseStream would have already enqueued the reset stream.
      // Currently only Aborts from application will actually enqueue
      // the reset stream here.
      enqueue_result = stream->EnqueueResetStream(
          static_cast<uint32_t>(Http2ErrorCode::kCancel));
      GRPC_HTTP2_CLIENT_DLOG << "Enqueued ResetStream with error code="
                             << static_cast<uint32_t>(Http2ErrorCode::kCancel)
                             << " status=" << enqueue_result.status();
    } else {
      enqueue_result = stream->EnqueueHalfClosed();
      GRPC_HTTP2_CLIENT_DLOG << "Enqueued HalfClosed with result="
                             << enqueue_result.status();
    }

    if (GPR_LIKELY(enqueue_result.ok())) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport::SetOnDone "
             "MaybeAddStreamToWritableStreamList for stream= "
          << stream->GetStreamId() << " enqueue_result={became_writable="
          << enqueue_result.value().became_writable << ", priority="
          << static_cast<uint8_t>(enqueue_result.value().priority) << "}";
      GRPC_UNUSED absl::Status status =
          self->MaybeAddStreamToWritableStreamList(std::move(stream),
                                                   enqueue_result.value());
    }
  });
}

std::optional<RefCountedPtr<Stream>> Http2ClientTransport::MakeStream(
    CallHandler call_handler) {
  RefCountedPtr<Stream> stream =
      MakeRefCounted<Stream>(call_handler, flow_control_);
  const bool on_done_added = SetOnDone(std::move(call_handler), stream);
  if (!on_done_added) return std::nullopt;
  return std::move(stream);
}

uint32_t Http2ClientTransport::GetMaxAllowedStreamId() const {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::GetMaxAllowedStreamId "
                         << max_allowed_stream_id_;
  return max_allowed_stream_id_;
}

void Http2ClientTransport::SetMaxAllowedStreamId(
    const uint32_t max_allowed_stream_id) {
  const uint32_t old_max_allowed_stream_id = GetMaxAllowedStreamId();
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::SetMaxAllowedStreamId "
                         << " max_allowed_stream_id: " << max_allowed_stream_id
                         << " old_allowed_max_stream_id: "
                         << old_max_allowed_stream_id;
  // RFC9113 : Endpoints MUST NOT increase the value they send in the last
  // stream identifier, since the peers might already have retried unprocessed
  // requests on another connection.
  if (GPR_LIKELY(max_allowed_stream_id <= old_max_allowed_stream_id)) {
    max_allowed_stream_id_ = max_allowed_stream_id;
  } else {
    GRPC_DCHECK_LE(max_allowed_stream_id, old_max_allowed_stream_id);
    LOG_IF(ERROR, max_allowed_stream_id > old_max_allowed_stream_id)
        << "Endpoints MUST NOT increase the value they send in the last "
           "stream identifier";
  }
}

///////////////////////////////////////////////////////////////////////////////
// Http2ClientTransport - Call Spine related operations

auto Http2ClientTransport::CallOutboundLoop(RefCountedPtr<Stream> stream) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::CallOutboundLoop";
  GRPC_DCHECK(stream != nullptr);

  auto send_message = [this, stream](MessageHandle&& message) mutable {
    return TrySeq(
        stream->EnqueueMessage(std::move(message)),
        [this, stream](const StreamWritabilityUpdate result) mutable {
          GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::CallOutboundLoop "
                                    "Enqueued Message";
          return MaybeAddStreamToWritableStreamList(std::move(stream), result);
        });
  };

  auto send_initial_metadata =
      [this, stream](ClientMetadataHandle&& metadata) mutable {
        absl::StatusOr<StreamWritabilityUpdate> enqueue_result =
            stream->EnqueueInitialMetadata(
                std::forward<ClientMetadataHandle>(metadata));
        if (GPR_UNLIKELY(!enqueue_result.ok())) {
          return enqueue_result.status();
        }
        GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::CallOutboundLoop "
                                  "Enqueued Initial Metadata";
        return MaybeAddStreamToWritableStreamList(std::move(stream),
                                                  enqueue_result.value());
      };

  auto send_half_closed = [this, stream]() mutable {
    absl::StatusOr<StreamWritabilityUpdate> enqueue_result =
        stream->EnqueueHalfClosed();
    if (GPR_UNLIKELY(!enqueue_result.ok())) {
      return enqueue_result.status();
    }
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::CallOutboundLoop "
                              "Enqueued Half Closed";
    return MaybeAddStreamToWritableStreamList(std::move(stream),
                                              enqueue_result.value());
  };

  return GRPC_LATENT_SEE_PROMISE(
      "Ph2CallOutboundLoop",
      TrySeq(
          Map(stream->GetCallHandler().PullClientInitialMetadata(),
              [send_initial_metadata = std::move(send_initial_metadata)](
                  ValueOrFailure<ClientMetadataHandle> metadata) mutable {
                if (GPR_UNLIKELY(!metadata.ok())) {
                  return absl::InternalError(
                      "Failed to pull client initial metadata");
                }
                return std::move(send_initial_metadata)(
                    TakeValue(std::move(metadata)));
              }),
          ForEach(MessagesFrom(stream->GetCallHandler()),
                  std::move(send_message)),
          [send_half_closed = std::move(send_half_closed)]() mutable {
            return std::move(send_half_closed)();
          },
          [this, stream]() mutable {
            return Map(
                stream->GetCallHandler().WasCancelled(),
                [this, stream](bool cancelled) mutable {
                  GRPC_HTTP2_CLIENT_DLOG
                      << "Http2ClientTransport::CallOutboundLoop End with "
                         "cancelled="
                      << cancelled;
                  if (cancelled) {
                    // Enqueue an RST_STREAM frame immediately upon call
                    // cancellation rather than waiting for CallHandler::OnDone.
                    absl::StatusOr<StreamWritabilityUpdate> enqueue_result =
                        stream->EnqueueResetStream(
                            static_cast<uint32_t>(Http2ErrorCode::kCancel));
                    GRPC_HTTP2_CLIENT_DLOG
                        << "Enqueued ResetStream with error code="
                        << static_cast<uint32_t>(Http2ErrorCode::kCancel)
                        << " status=" << enqueue_result.status();
                    if (GPR_LIKELY(enqueue_result.ok())) {
                      GRPC_UNUSED absl::Status status =
                          MaybeAddStreamToWritableStreamList(
                              std::move(stream), enqueue_result.value());
                    }
                  }
                  return absl::OkStatus();
                });
          }));
}

void Http2ClientTransport::StartCall(CallHandler call_handler) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::StartCall Begin";

  call_handler.SpawnGuarded(
      "OutboundLoop",
      [self = RefAsSubclass<Http2ClientTransport>(), call_handler]() mutable {
        std::optional<RefCountedPtr<Stream>> stream =
            self->MakeStream(std::move(call_handler));

        return If(
            stream.has_value(),
            [self = std::move(self), stream]() mutable {
              return Map(self->CallOutboundLoop(std::move(stream.value())),
                         [self](absl::Status status) { return status; });
            },
            []() { return absl::InternalError("Failed to make stream"); });
      });
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::StartCall End";
}

///////////////////////////////////////////////////////////////////////////////
// Http2ClientTransport - Test Only Functions

int64_t Http2ClientTransport::TestOnlyTransportFlowControlWindow() {
  return flow_control_.remote_window();
}

int64_t Http2ClientTransport::TestOnlyGetStreamFlowControlWindow(
    const uint32_t stream_id) {
  RefCountedPtr<Stream> stream = LookupStream(stream_id);
  if (stream == nullptr) {
    return -1;
  }
  return stream->GetStreamFlowControl().remote_window_delta();
}

////////////////////////////////////////////////////////////////////////////////
// Http2ClientTransport - Ping Helpers

void Http2ClientTransport::MaybeSpawnPingTimeout(
    std::optional<uint64_t> opaque_data) {
  if (opaque_data.has_value()) {
    SpawnGuardedTransportParty(
        "PingTimeout", [self = RefAsSubclass<Http2ClientTransport>(),
                        opaque_data = *opaque_data]() {
          return self->ping_manager_->TimeoutPromise(opaque_data);
        });
  }
}
void Http2ClientTransport::MaybeSpawnDelayedPing(
    std::optional<Duration> delayed_ping_wait) {
  if (delayed_ping_wait.has_value()) {
    SpawnGuardedTransportParty(
        "DelayedPing", [self = RefAsSubclass<Http2ClientTransport>(),
                        wait = *delayed_ping_wait]() {
          GRPC_HTTP2_PING_LOG << "Scheduling delayed ping after wait=" << wait;
          return self->ping_manager_->DelayedPingPromise(wait);
        });
  }
}

void Http2ClientTransport::MaybeSpawnKeepaliveLoop() {
  if (keepalive_manager_->IsKeepAliveLoopNeeded()) {
    SpawnGuardedTransportParty(
        "KeepaliveLoop", [self = RefAsSubclass<Http2ClientTransport>()]() {
          return self->keepalive_manager_->KeepaliveLoop();
        });
  }
}

///////////////////////////////////////////////////////////////////////////////
// Class PingSystemInterfaceImpl

std::unique_ptr<PingInterface>
Http2ClientTransport::PingSystemInterfaceImpl::Make(
    Http2ClientTransport* transport) {
  return std::make_unique<PingSystemInterfaceImpl>(
      PingSystemInterfaceImpl(transport));
}

absl::Status Http2ClientTransport::PingSystemInterfaceImpl::TriggerWrite() {
  return transport_->TriggerWriteCycle();
}

Promise<absl::Status>
Http2ClientTransport::PingSystemInterfaceImpl::PingTimeout() {
  GRPC_HTTP2_CLIENT_DLOG << "PingSystemInterfaceImpl::PingTimeout at time: "
                         << Timestamp::Now();

  // TODO(akshitpatel) : [PH2][P2] : The error code here has been chosen
  // based on CHTTP2's usage of GRPC_STATUS_UNAVAILABLE (which corresponds
  // to kRefusedStream). However looking at RFC9113, definition of
  // kRefusedStream doesn't seem to fit this case. We should revisit this
  // and update the error code.
  return Immediate(transport_->HandleError(
      nullptr,
      Http2Status::Http2ConnectionError(Http2ErrorCode::kRefusedStream,
                                        GRPC_CHTTP2_PING_TIMEOUT_STR)));
}

///////////////////////////////////////////////////////////////////////////////
// Class KeepAliveInterfaceImpl

std::unique_ptr<KeepAliveInterface>
Http2ClientTransport::KeepAliveInterfaceImpl::Make(
    Http2ClientTransport* transport) {
  return std::make_unique<KeepAliveInterfaceImpl>(
      KeepAliveInterfaceImpl(transport));
}

Promise<absl::Status>
Http2ClientTransport::KeepAliveInterfaceImpl::SendPingAndWaitForAck() {
  return TrySeq(
      [transport = transport_] { return transport->TriggerWriteCycle(); },
      [transport = transport_] { return transport->WaitForPingAck(); });
}

Promise<absl::Status>
Http2ClientTransport::KeepAliveInterfaceImpl::OnKeepAliveTimeout() {
  GRPC_HTTP2_CLIENT_DLOG
      << "KeepAliveInterfaceImpl::OnKeepAliveTimeout triggered";
  // TODO(akshitpatel) : [PH2][P2] : The error code here has been chosen
  // based on CHTTP2's usage of GRPC_STATUS_UNAVAILABLE (which corresponds
  // to kRefusedStream). However looking at RFC9113, definition of
  // kRefusedStream doesn't seem to fit this case. We should revisit this
  // and update the error code.
  return Immediate(transport_->HandleError(
      nullptr,
      Http2Status::Http2ConnectionError(Http2ErrorCode::kRefusedStream,
                                        GRPC_CHTTP2_KEEPALIVE_TIMEOUT_STR)));
}

bool Http2ClientTransport::KeepAliveInterfaceImpl::NeedToSendKeepAlivePing() {
  bool need_to_send_ping = false;
  {
    MutexLock lock(&transport_->transport_mutex_);
    need_to_send_ping = (transport_->keepalive_permit_without_calls_ ||
                         transport_->GetActiveStreamCountLocked() > 0);
  }
  return need_to_send_ping;
}

///////////////////////////////////////////////////////////////////////////////
// Class GoawayInterfaceImpl

std::unique_ptr<GoawayInterface>
Http2ClientTransport::GoawayInterfaceImpl::Make(
    Http2ClientTransport* transport) {
  return std::make_unique<GoawayInterfaceImpl>(GoawayInterfaceImpl(transport));
}

uint32_t Http2ClientTransport::GoawayInterfaceImpl::GetLastAcceptedStreamId() {
  LOG(DFATAL) << "GetLastAcceptedStreamId is not implemented for client "
                 "transport.";
  return 0;
}

// TODO(akshitpatel) : [PH2][P2] : Eventually there should be a separate ref
// counted struct/class passed to all the transport promises/members. This
// will help removing back references from the transport members to
// transport and greatly simplify the cleanup path. Need to do this for
// PingSystemInterfaceImpl, KeepAliveInterfaceImpl and GoawayInterfaceImpl.

}  // namespace http2
}  // namespace grpc_core
