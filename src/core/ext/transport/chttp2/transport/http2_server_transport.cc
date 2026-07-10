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

#include "src/core/ext/transport/chttp2/transport/http2_server_transport.h"

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
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/call/call_destination.h"
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
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
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
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace_impl.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
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
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {
namespace http2 {

using grpc_event_engine::experimental::EventEngine;
using StreamWritabilityUpdate =
    StreamDataQueue<ServerMetadataHandle>::StreamWritabilityUpdate;

// Experimental : This is just the initial skeleton of class
// and it is functions. The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P3] : Delete this comment after CHTTP2 deletion.

constexpr bool kIsClient = false;

//////////////////////////////////////////////////////////////////////////////
// Channelz and ZTrace

RefCountedPtr<channelz::SocketNode> Http2ServerTransport::GetSocketNode()
    const {
  const channelz::BaseNode* node = channelz::DataSource::channelz_node();
  if (node == nullptr) {
    return nullptr;
  }
  return const_cast<channelz::BaseNode*>(node)
      ->RefAsSubclass<channelz::SocketNode>();
}

void Http2ServerTransport::AddData(channelz::DataSink sink) {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::AddData Begin";

  event_engine_->Run([self = RefAsSubclass<Http2ServerTransport>(),
                      sink = std::move(sink)]() mutable {
    RefCountedPtr<Party> party = nullptr;
    {
      MutexLock lock(&self->transport_mutex_);
      if (GPR_LIKELY(!self->is_transport_closed_)) {
        GRPC_DCHECK(self->general_party_ != nullptr);
        party = self->general_party_;
      } else {
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::AddData Transport is closed.";
      }
    }

    ExecCtx exec_ctx;
    if (party != nullptr) {
      self->SpawnAddChannelzData(std::move(party), std::move(sink));
    }
    self.reset();  // Cleanup with exec_ctx in scope
  });
}

void Http2ServerTransport::SpawnAddChannelzData(RefCountedPtr<Party> party,
                                                channelz::DataSink sink) {
  SpawnInfallible(
      std::move(party), "AddData",
      [self = RefAsSubclass<Http2ServerTransport>(),
       sink = std::move(sink)]() mutable {
        GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::SpawnAddChannelzData";
        sink.AddData(
            "Http2ServerTransport",
            channelz::PropertyList()
                .Set("keepalive_time", self->keepalive_time_)
                .Set("keepalive_permit_without_calls",
                     self->keepalive_permit_without_calls_)
                .Set("max_requests_per_read",
                     self->read_context_.max_new_streams_per_read_cycle())
                .Set("settings", self->settings_->ChannelzProperties())
                .Set("flow_control",
                     self->flow_control_.stats().ChannelzProperties()));
        self->general_party_->ExportToChannelz("Http2ServerTransport Party",
                                               sink);
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::SpawnAddChannelzData End";
        return Empty{};
      });
}

//////////////////////////////////////////////////////////////////////////////
// Watchers

void Http2ServerTransport::StartWatch(RefCountedPtr<StateWatcher> watcher) {
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

void Http2ServerTransport::StopWatch(RefCountedPtr<StateWatcher> watcher) {
  MutexLock lock(&transport_mutex_);
  if (watcher_ == watcher) watcher_.reset();
}

void Http2ServerTransport::StartConnectivityWatch(
    grpc_connectivity_state state,
    OrphanablePtr<ConnectivityStateWatcherInterface> watcher) {
  MutexLock lock(&transport_mutex_);
  state_tracker_.AddWatcher(state, std::move(watcher));
}

void Http2ServerTransport::StopConnectivityWatch(
    ConnectivityStateWatcherInterface* watcher) {
  MutexLock lock(&transport_mutex_);
  state_tracker_.RemoveWatcher(watcher);
}

void Http2ServerTransport::NotifyStateWatcherOnDisconnectLocked(
    absl::Status status, StateWatcher::DisconnectInfo disconnect_info) {
  if (watcher_ == nullptr) return;
  event_engine_->Run([watcher = std::move(watcher_), status = std::move(status),
                      disconnect_info]() mutable {
    ExecCtx exec_ctx;
    watcher->OnDisconnect(std::move(status), disconnect_info);
    watcher.reset();  // Before ExecCtx goes out of scope.
  });
}

//////////////////////////////////////////////////////////////////////////////
// Test Only Functions

int64_t Http2ServerTransport::TestOnlyTransportFlowControlWindow() {
  return flow_control_.remote_window();
}

int64_t Http2ServerTransport::TestOnlyGetStreamFlowControlWindow(
    const uint32_t stream_id) {
  RefCountedPtr<Stream> stream = LookupStream(stream_id);
  if (stream == nullptr) {
    return -1;
  }
  return stream->GetStreamFlowControl().remote_window_delta();
}

//////////////////////////////////////////////////////////////////////////////
// Endpoint Helpers

auto Http2ServerTransport::EndpointWrite(SliceBuffer&& output_buf) {
  size_t output_buf_length = output_buf.Length();
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::EndpointWrite output_buf: "
                         << output_buf_length;

  transport_write_context_.GetWriteCycle().BeginWrite(output_buf_length);
  return Map(
      endpoint_.Write(std::forward<SliceBuffer>(output_buf),
                      TransportWriteContext::GetWriteArgs(settings_->peer())),
      [this](absl::Status status) {
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::EndpointWrite complete with status = "
            << status;
        transport_write_context_.GetWriteCycle().EndWrite(status.ok());
        return status;
      });
}

auto Http2ServerTransport::SerializeAndWrite() {
  return AssertResultType<absl::Status>(If(
      transport_write_context_.GetWriteCycle().CanSerializeRegularFrames(),
      [this]() mutable {
        WriteCycle& write_cycle = transport_write_context_.GetWriteCycle();
        const uint64_t frame_count = write_cycle.GetRegularFrameCount();
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::SerializeAndWrite frame count: "
            << frame_count;
        ztrace_collector_->Append(PromiseEndpointWriteTrace{frame_count});
        return EndpointWrite(write_cycle.SerializeRegularFrames(
            WriteCycle::SerializeStats{should_reset_ping_clock_}));
      },
      []() { return absl::OkStatus(); }));
}

//////////////////////////////////////////////////////////////////////////////
// Transport Read Path

Http2Status Http2ServerTransport::ProcessIncomingFrame(Http2DataFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-data
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) { stream_id="
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
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) { stream_id="
        << frame.stream_id << "} Lookup Failed";
    return Http2Status::Ok();
  }

  // TODO(akshitpatel) : [PH2][P3] : We should add a check to reset stream if
  // the stream state is kIdle as well.

  Http2Status stream_status = stream->CanStreamReceiveDataFrames();
  if (!stream_status.IsOk()) {
    return stream_status;
  }

  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) "
         "AppendNewDataFrame";
  GrpcMessageAssembler& assembler = stream->GetGrpcMessageAssembler();
  Http2Status status =
      assembler.AppendNewDataFrame(frame.payload, frame.end_stream);
  if (!status.IsOk()) {
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) "
           "AppendNewDataFrame Failed";
    return status;
  }

  // Pass the messages up the stack if it is ready.
  while (true) {
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) "
           "ExtractMessage";
    ValueOrHttp2Status<MessageHandle> result = assembler.ExtractMessage();
    if (!result.IsOk()) {
      GRPC_HTTP2_SERVER_DLOG
          << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) "
             "ExtractMessage Failed";
      return ValueOrHttp2Status<MessageHandle>::TakeStatus(std::move(result));
    }
    MessageHandle message = TakeValue(std::move(result));
    if (message != nullptr) {
      GRPC_HTTP2_SERVER_DLOG
          << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) "
             "SpawnPushMessage ";
      stream->GetCallInitiator().SpawnPushMessage(std::move(message));
      continue;
    }
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) While Break";
    break;
  }

  if (frame.end_stream) {
    HandleStreamStateChange(*stream, stream->OnHalfCloseReceived());
  }
  return Http2Status::Ok();
}

template <typename T>
Http2Status Http2ServerTransport::ProcessIncomingMetadata(T&& frame) {
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingMetadata { stream_id="
      << frame.stream_id << ", end_headers=" << frame.end_headers << " }";
  ping_manager_->ReceivedDataFrame();

  bool is_new_stream = false;
  RefCountedPtr<Stream> stream = nullptr;
  // State update MUST happen before processing the frame.
  if (!read_context_.IsWaitingForContinuationFrame()) {
    // This is a HEADERS frame.
    stream = LookupStream(frame.stream_id);
    is_new_stream = (stream == nullptr);
    // TODO(tjagtap) : [PH2][P2] : Implement initial stream id checks for new
    // streams.
    last_incoming_stream_id_ = frame.stream_id;
  } else {
    // This is a CONTINUATION frame.
    GRPC_DCHECK(read_context_.GetStreamId() == frame.stream_id);
    GRPC_DCHECK(LookupStream(frame.stream_id) != nullptr);
    is_new_stream = true;
  }
  read_context_.UpdateState(frame, /*is_existing_stream=*/!is_new_stream);

  if (is_new_stream) {
    // TODO(tjagtap) : [PH2][P3] : Implement this.
    // RFC9113 : The identifier of a newly established stream MUST be
    // numerically greater than all streams that the initiating endpoint has
    // opened or reserved. This governs streams that are opened using a HEADERS
    // frame and streams that are reserved using PUSH_PROMISE. An endpoint that
    // receives an unexpected stream identifier MUST respond with a connection
    // error (Section 5.4.1) of type PROTOCOL_ERROR.

    if (goaway_manager_.IsFinalGracefulGoawayScheduledOrSent()) {
      return read_context_.ParseAndDiscardHeaders(
          std::move(frame.payload), frame.end_headers, Http2Status::Ok(),
          settings_->acked().max_header_list_size());
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
    Http2Status status = ProcessMetadata();
    if (!status.IsOk()) {
      // Frame payload has been moved to the HeaderAssembler. So calling
      // ParseAndDiscardHeaders with an empty buffer.
      return read_context_.ParseAndDiscardHeaders(
          SliceBuffer(), frame.end_headers, std::move(status),
          settings_->acked().max_header_list_size());
    }
  } else {
    // Stream already exists.
    // TODO(tjagtap) : [PH2][P1] : Implement/Verify this
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::ProcessIncomingMetadata { stream_id="
        << frame.stream_id << "} Stream already exists.";
    Http2Status validation_status =
        ValidateMetadataFrameState(frame, *stream, read_context_,
                                   settings_->acked().max_header_list_size());
    if (!validation_status.IsOk()) {
      return validation_status;
    }
  }
  // Frame payload has either been processed or moved to the HeaderAssembler.
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    Http2HeaderFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-headers
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(HeaderFrame) end_stream="
      << frame.end_stream;
  return ProcessIncomingMetadata(std::forward<Http2HeaderFrame>(frame));
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    Http2RstStreamFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-rst_stream
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(ResetStreamFrame) { "
         "stream_id="
      << frame.stream_id << ", error_code=" << frame.error_code << " }";
  read_context_.OnResetFrameReceived();

  Http2ErrorCode error_code = FrameErrorCodeToHttp2ErrorCode(frame.error_code);
  absl::Status status = absl::Status(ErrorCodeToAbslStatusCode(error_code),
                                     "Reset stream frame received.");
  RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
  if (stream != nullptr) {
    if (status.ok()) {
      status =
          absl::UnavailableError("RST_STREAM frame received with no error.");
    }

    HandleStreamStateChange(*stream,
                            stream->OnResetReceived(std::move(status)));
  }

  // In case of stream error, we do not want the Read Loop to be broken. Hence
  // returning an ok status.
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    Http2SettingsFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-settings

  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(SettingsFrame) { ack="
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

Http2Status Http2ServerTransport::ProcessIncomingFrame(Http2PingFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-ping
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(PingFrame) { ack="
      << frame.ack << ", opaque=" << frame.opaque << " }";
  if (frame.ack) {
    return ToHttpOkOrConnError(AckPing(frame.opaque));
  } else {
    read_context_.OnPingFrameReceived();
    if (test_only_ack_pings_) {
      ping_manager_->AddPendingPingAck(frame.opaque);
      return ToHttpOkOrConnError(TriggerWriteCycle());
    } else {
      GRPC_HTTP2_SERVER_DLOG
          << "Http2ServerTransport::ProcessIncomingFrame(PingFrame) "
             "test_only_ack_pings_ is false. Ignoring the ping request.";
    }
  }
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    Http2GoawayFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-goaway
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(GoawayFrame) { "
         "last_stream_id="
      << frame.last_stream_id << ", error_code=" << frame.error_code << "}";
  LOG_IF(ERROR,
         frame.error_code != static_cast<uint32_t>(Http2ErrorCode::kNoError))
      << "Received GOAWAY frame with error code: " << frame.error_code;

  //   uint32_t last_stream_id = 0;
  //   absl::Status status(ErrorCodeToAbslStatusCode(
  //                           FrameErrorCodeToHttp2ErrorCode(frame.error_code)),
  //                       frame.debug_data.empty()
  //                           ? absl::string_view("GOAWAY received")
  //                           : frame.debug_data.as_string_view());
  //   if (GoawayManager::IsGracefulGoaway(frame)) {
  //     const uint32_t next_stream_id = PeekNextStreamId();
  //     last_stream_id = (next_stream_id > 1) ? next_stream_id - 2 : 0;
  //   } else {
  //     last_stream_id = frame.last_stream_id;
  //   }
  //   SetMaxAllowedStreamId(last_stream_id);

  //   bool close_transport = false;
  //   {
  //     MutexLock lock(&transport_mutex_);
  //     if (CanCloseTransportLocked()) {
  //       close_transport = true;
  //       GRPC_HTTP2_SERVER_DLOG <<
  //       "Http2ServerTransport::ProcessIncomingFrame("
  //                                 "GoawayFrame) "
  //                                 "stream_list_ is empty";
  //     }
  //   }

  //   StateWatcher::DisconnectInfo disconnect_info;
  //   disconnect_info.reason = Transport::StateWatcher::kGoaway;
  //   disconnect_info.http2_error_code =
  //       static_cast<Http2ErrorCode>(frame.error_code);

  //   // Throttle keepalive time if the server sends a GOAWAY with error code
  //   // ENHANCE_YOUR_CALM and debug data equal to "too_many_pings". This
  //   will
  //   // apply to any new transport created on by any subchannel of this
  //   channel. if (GPR_UNLIKELY(frame.error_code == static_cast<uint32_t>(
  //                                            Http2ErrorCode::kEnhanceYourCalm)
  //                                            &&
  //                    frame.debug_data == "too_many_pings")) {
  //     LOG(ERROR) << ": Received a GOAWAY with error code ENHANCE_YOUR_CALM
  //     and
  //     "
  //                   "debug data equal to \"too_many_pings\". Current
  //                   keepalive " "time (before throttling): "
  //                << keepalive_time_.ToString();
  //     constexpr int max_keepalive_time_millis =
  //         INT_MAX / KEEPALIVE_TIME_BACKOFF_MULTIPLIER;
  //     uint64_t throttled_keepalive_time =
  //         keepalive_time_.millis() > max_keepalive_time_millis
  //             ? INT_MAX
  //             : keepalive_time_.millis() *
  //             KEEPALIVE_TIME_BACKOFF_MULTIPLIER;
  //     if (!IsSubchannelConnectionScalingEnabled()) {
  //       status.SetPayload(kKeepaliveThrottlingKey,
  //                         absl::Cord(std::to_string(throttled_keepalive_time)));
  //     }
  //     disconnect_info.keepalive_time =
  //         Duration::Milliseconds(throttled_keepalive_time);
  //   }

  //   if (close_transport) {
  //     // TODO(akshitpatel) : [PH2][P3] : Ideally the error here should be
  //     // kNoError. However, Http2Status does not support kNoError. We
  //     should
  //     // revisit this and update the error code.
  //     MaybeSpawnCloseTransport(Http2Status::Http2ConnectionError(
  //         FrameErrorCodeToHttp2ErrorCode((
  //             frame.error_code ==
  //                     Http2ErrorCodeToFrameErrorCode(Http2ErrorCode::kNoError)
  //                 ?
  //                 Http2ErrorCodeToFrameErrorCode(Http2ErrorCode::kInternalError)
  //                 : frame.error_code)),
  //         frame.debug_data.empty()
  //             ? std::string("GOAWAY received")
  //             : std::string(frame.debug_data.as_string_view())));
  //   }

  //   // lie: use transient failure from the transport to indicate goaway has
  //   been
  //   // received.
  //   ReportDisconnection(status, disconnect_info, "got_goaway");
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    Http2WindowUpdateFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-window_update
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(WindowUpdateFrame) { "
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

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    Http2ContinuationFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-continuation
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(ContinuationFrame)";
  return ProcessIncomingMetadata(std::forward<Http2ContinuationFrame>(frame));
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    Http2SecurityFrame&& frame) {
  if (settings_->IsSecurityFrameExpected()) {
    security_frame_handler_->ProcessPayload(std::move(frame.payload));
  }
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    GRPC_UNUSED Http2UnknownFrame&& frame) {
  // RFC9113: Implementations MUST ignore and discard frames of
  // unknown types.
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(UnknownFrame) ";
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    GRPC_UNUSED Http2EmptyFrame&& frame) {
  LOG(DFATAL) << "ParseFramePayload should never return a Http2EmptyFrame";
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessMetadata() {
  HeaderAssembler& assembler = read_context_.header_assembler();
  // CallInitiator& call = stream->GetCallInitiator();

  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::ProcessMetadata";
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
      // TODO(tjagtap): [PH2][P0] : Might be worth differentiating between
      // initial and trailing metadata based on the number of header frames
      // received.
      if (read_context_.HeaderHasEndStream()) {
        // TODO(akshitpatel) [PH2][P1] : Implement receiving trailing metadata.
        // Details:
        // - Standard gRPC clients do not send trailers (only EOS).
        // - If received (HEADERS with END_STREAM), mark stream as half-closed
        //   remote.
        // - Upper layers discard client trailers, so we are fine with not
        //   propagating them.
        //
        // With these assumptions, the flow will look like this:
        // - If the client sends trailing metadata with an OK status, we will
        //   mark the stream as half-closed remote and do nothing else.
        // - If the client sends trailing metadata with a non-OK status, this
        //   case needs to be handled.
        // TODO(akshitpatel) : [PH2][P0] : Verify this.
        RefCountedPtr<Stream> stream =
            LookupStream(read_context_.GetStreamId());
        HandleStreamStateChange(
            *stream, stream->OnTrailingMetadataReceived(std::move(metadata)));
      } else {
        GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::ProcessMetadata "
                                  "SpawnPushServerInitialMetadata";
        metadata->Set(PeerString(), read_context_.peer_string());
        return IncomingStream(std::move(metadata), read_context_.GetStreamId());
      }
      return Http2Status::Ok();
    }
    GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::ProcessMetadata Failed";
    return ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>>::TakeStatus(
        std::move(read_result));
  }
  return Http2Status::Ok();
}

auto Http2ServerTransport::ReadAndProcessOneFrame() {
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ReadAndProcessOneFrame Factory";
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
            // TODO(tjagtap) : [PH2][P0] : Fix
            /*last_stream_id=*//*GetLastStreamId()*/
            std::numeric_limits<uint32_t>::max(),
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
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::ReadAndProcessOneFrame Read Frame ";
        return AssertResultType<absl::Status>(
            Map(EndpointRead(read_context_.GetCurrentFrameHeader().length),
                [this](absl::StatusOr<SliceBuffer>&& payload) {
                  if (GPR_UNLIKELY(!payload.ok())) {
                    return payload.status();
                  }
                  GRPC_HTTP2_SERVER_DLOG
                      << "Http2ServerTransport::ReadAndProcessOneFrame "
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

auto Http2ServerTransport::ReadLoop() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::ReadLoop Factory";
  return AssertResultType<absl::Status>(Loop([this]() {
    return TrySeq(ReadAndProcessOneFrame(), []() -> LoopCtl<absl::Status> {
      GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::ReadLoop Continue";
      return Continue();
    });
  }));
}

//////////////////////////////////////////////////////////////////////////////
// Transport Write Path

absl::Status Http2ServerTransport::PrepareControlFrames() {
  FrameSender frame_sender =
      transport_write_context_.GetWriteCycle().GetFrameSender();
  if (transport_write_context_.IsFirstWrite()) {
    // Send the first settings frame.
    settings_->MaybeGetSettingsAndSettingsAckFrames(flow_control_,
                                                    frame_sender);
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

auto Http2ServerTransport::MaybeWriteUrgentFrames() {
  return AssertResultType<absl::Status>(If(
      transport_write_context_.GetWriteCycle().CanSerializeUrgentFrames(),
      [this]() mutable {
        WriteCycle& write_cycle = transport_write_context_.GetWriteCycle();
        const uint64_t buffer_length = write_cycle.GetUrgentFrameCount();
        ztrace_collector_->Append(PromiseEndpointWriteTrace{buffer_length});
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::MaybeWriteUrgentFrames frame count: "
            << buffer_length;
        return EndpointWrite(write_cycle.SerializeUrgentFrames(
            WriteCycle::SerializeStats{should_reset_ping_clock_}));
      },
      []() { return absl::OkStatus(); }));
}

void Http2ServerTransport::NotifyFramesWriteDone() {
  // Notify Control modules that we have sent the frames.
  // All notifications are expected to be synchronous.
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::NotifyFramesWriteDone";
  read_context_.ResumeReadLoopIfPaused();
  MaybeSpawnPingTimeout(ping_manager_->NotifyPingSent());
  goaway_manager_.NotifyGoawaySent();
  MaybeSpawnWaitForSettingsTimeout();
}

void Http2ServerTransport::NotifyUrgentFramesWriteDone() {}

absl::Status Http2ServerTransport::DequeueStreamFrames(
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
      GRPC_HTTP2_SERVER_DLOG
          << "Http2ServerTransport::DequeueStreamFrames Failed to "
             "enqueue stream "
          << stream->GetStreamId() << " with status: " << status;
      // Close transport if we fail to enqueue stream.
      return HandleError(/*stream=*/nullptr, ToHttpOkOrConnError(status));
    }
  }

  if (result.IsInitialMetadataDequeued()) {
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::DequeueStreamFrames InitialMetadataDequeued "
           "stream_id = "
        << stream->GetStreamId();
  }

  if (result.IsTrailingMetadataDequeued()) {
    GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::DequeueStreamFrames "
                              "TrailingMetadataDequeued stream_id = "
                           << stream->GetStreamId();
    // Stream is not marked closed here as TrailingMetadata is always followed
    // by RST_STREAM and we close the stream when we dequeue the RST_STREAM.
  }

  if (result.IsResetStreamDequeued()) {
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::DequeueStreamFrames ResetStreamDequeued "
           "stream_id = "
        << stream->GetStreamId();
    // As Trailing metadata is already read from CallInitiator, no need to send
    // a status to the application.
    HandleStreamStateChange(*stream, stream->OnResetSent());
  }

  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::DequeueStreamFrames "
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
auto Http2ServerTransport::MultiplexerLoop() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::MultiplexerLoop Factory";
  return AssertResultType<absl::Status>(Loop([this]() {
    return TrySeq(
        Map(writable_stream_list_.WaitForReady(
                AreTransportFlowControlTokensAvailable()),
            [this](absl::StatusOr<Empty> status) -> absl::Status {
              if (GPR_UNLIKELY(!status.ok())) {
                return status.status();
              }
              transport_write_context_.StartWriteCycle();
              GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::MultiplexerLoop "
                                        "Start Iteration: "
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
                    << "Http2ServerTransport::MultiplexerLoop "
                       "No writable streams available ";
                break;
              }
              RefCountedPtr<Stream> stream = std::move(optional_stream.value());
              GRPC_HTTP2_SERVER_DLOG
                  << "Http2ServerTransport::MultiplexerLoop "
                     "Next writable stream id = "
                  << stream->GetStreamId()
                  << " is_closed_for_writes = " << stream->IsClosedForWrites();
              GRPC_DCHECK_NE(stream->GetStreamId(), kInvalidStreamId);

              if (GPR_LIKELY(!stream->IsClosedForWrites())) {
                absl::Status status = DequeueStreamFrames(
                    std::move(stream),
                    transport_write_context_.GetWriteCycle());
                if (GPR_UNLIKELY(!status.ok())) {
                  GRPC_HTTP2_SERVER_DLOG
                      << "Http2ServerTransport::MultiplexerLoop "
                         "Failed to dequeue stream frames with status: "
                      << status;
                  return status;
                }
              }
            }

            GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::MultiplexerLoop "
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

//////////////////////////////////////////////////////////////////////////////
// Settings

void Http2ServerTransport::EnforceLatestIncomingSettings() {
  encoder_.SetMaxTableSize(settings_->peer().header_table_size());
}

auto Http2ServerTransport::WaitForSettingsTimeoutOnDone() {
  return [self = RefAsSubclass<Http2ServerTransport>()](absl::Status status) {
    if (!status.ok()) {
      GRPC_UNUSED absl::Status result = self->HandleError(
          /*stream=*/nullptr, Http2Status::Http2ConnectionError(
                                  Http2ErrorCode::kProtocolError,
                                  std::string(RFC9113::kSettingsTimeout)));
    }
  };
}

void Http2ServerTransport::MaybeSpawnWaitForSettingsTimeout() {
  if (settings_->ShouldSpawnWaitForSettingsTimeout()) {
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::MaybeSpawnWaitForSettingsTimeout Spawning";
    SpawnWithOnDoneTransportParty("WaitForSettingsTimeout",
                                  settings_->WaitForSettingsTimeout(),
                                  WaitForSettingsTimeoutOnDone());
  }
}

//////////////////////////////////////////////////////////////////////////////
// Flow Control and BDP

// Equivalent to grpc_chttp2_act_on_flowctl_action in chttp2_transport.cc
void Http2ServerTransport::ActOnFlowControlAction(
    const chttp2::FlowControlAction& action, Stream* stream) {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::ActOnFlowControlAction"
                         << action.DebugString();
  if (action.send_stream_update() != kNoActionNeeded) {
    if (GPR_LIKELY(stream != nullptr)) {
      GRPC_DCHECK_GT(stream->GetStreamId(), 0u);
      if (stream->CanSendWindowUpdateFrames()) {
        flow_control_.AddStreamToWindowUpdateList(stream->GetStreamId());
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::ActOnFlowControlAction "
               "added stream "
            << stream->GetStreamId() << " to window_update_list_";
      }
    } else {
      GRPC_HTTP2_SERVER_DLOG
          << "Http2ServerTransport::ActOnFlowControlAction stream is null";
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

    GRPC_HTTP2_SERVER_DLOG << "Update Immediately : "
                           << action.ImmediateUpdateReasons();
  }
}

void Http2ServerTransport::MaybeGetWindowUpdateFrames(
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

auto Http2ServerTransport::FlowControlPeriodicUpdateLoop() {
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::FlowControlPeriodicUpdateLoop Factory";
  return AssertResultType<absl::Status>(
      Loop([this]() {
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::FlowControlPeriodicUpdateLoop Loop";
        return TrySeq(
            // TODO(tjagtap) [PH2][P2][BDP] Remove this static sleep when the
            // BDP code is done.
            Sleep(chttp2::kFlowControlPeriodicUpdateTimer),
            [this]() -> Poll<absl::Status> {
              GRPC_HTTP2_SERVER_DLOG
                  << "Http2ServerTransport::FlowControlPeriodicUpdateLoop "
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

//////////////////////////////////////////////////////////////////////////////
// Stream List Operations

RefCountedPtr<Stream> Http2ServerTransport::LookupStream(uint32_t stream_id) {
  MutexLock lock(&transport_mutex_);
  auto it = stream_list_.find(stream_id);
  if (it == stream_list_.end()) {
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::LookupStream Stream not found stream_id="
        << stream_id;
    return nullptr;
  }
  return it->second;
}

void Http2ServerTransport::AddToStreamList(RefCountedPtr<Stream> stream) {
  bool should_wake_periodic_updates = false;
  {
    MutexLock lock(&transport_mutex_);
    GRPC_DCHECK(stream != nullptr) << "stream is null";
    GRPC_DCHECK_GT(stream->GetStreamId(), 0u) << "stream id is invalid";
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::AddToStreamList for stream id: "
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

absl::Status Http2ServerTransport::MaybeAddStreamToWritableStreamList(
    RefCountedPtr<Stream> stream,
    const StreamDataQueue<ServerMetadataHandle>::StreamWritabilityUpdate
        result) {
  if (result.became_writable) {
    GRPC_HTTP2_SERVER_DLOG
        << "Http2ServerTransport::MaybeAddStreamToWritableStreamList Stream "
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

//////////////////////////////////////////////////////////////////////////////
// Stream Operations
auto Http2ServerTransport::HandleMetadataAndMessages(
    RefCountedPtr<Stream> stream) {
  auto send_message = [this, stream](MessageHandle&& message) mutable {
    return TrySeq(HandleStreamErrorOnFailure(
                      stream->EnqueueMessage(std::move(message)), stream),
                  [this, stream](const StreamWritabilityUpdate result) mutable {
                    GRPC_HTTP2_SERVER_DLOG
                        << "Http2ServerTransport::HandleMetadataAndMessages "
                           "Enqueued Message";
                    return MaybeAddStreamToWritableStreamList(std::move(stream),
                                                              result);
                  });
  };

  auto send_initial_metadata = [this, stream](
                                   ServerMetadataHandle&& metadata) mutable {
    absl::StatusOr<StreamWritabilityUpdate> enqueue_result =
        stream->EnqueueInitialMetadata(
            std::forward<ServerMetadataHandle>(metadata));
    if (GPR_UNLIKELY(!enqueue_result.ok())) {
      absl::Status status = enqueue_result.status();
      GRPC_UNUSED absl::Status unused = HandleError(
          stream, Http2Status::AbslStreamError(status.code(),
                                               std::string(status.message())));
      return status;
    }
    GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::HandleMetadataAndMessages "
                              "Enqueued Initial Metadata";
    return MaybeAddStreamToWritableStreamList(std::move(stream),
                                              enqueue_result.value());
  };

  return TrySeq(
      Map(stream->GetCallInitiator().PullServerInitialMetadata(),
          [send_initial_metadata = std::move(send_initial_metadata)](
              std::optional<ServerMetadataHandle> initial_metadata) mutable {
            if (initial_metadata.has_value()) {
              return send_initial_metadata(std::move(initial_metadata).value());
            }
            return absl::OkStatus();
          }),
      ForEach(MessagesFrom(stream->GetCallInitiator()),
              std::move(send_message)));
}

auto Http2ServerTransport::CallOutboundLoop(RefCountedPtr<Stream> stream) {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::CallOutboundLoop";
  GRPC_DCHECK(stream != nullptr);

  auto send_trailing_metadata =
      [this, stream](ServerMetadataHandle&& metadata) mutable {
        absl::StatusOr<StreamWritabilityUpdate> enqueue_result =
            stream->EnqueueTrailingMetadata(std::move(metadata));
        if (GPR_UNLIKELY(!enqueue_result.ok())) {
          GRPC_HTTP2_SERVER_DLOG
              << "Http2ServerTransport::CallOutboundLoop Failed to enqueue "
                 "trailing metadata: "
              << enqueue_result.status();
          return enqueue_result.status();
        }
        GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::CallOutboundLoop "
                                  "Enqueued Trailing Metadata";
        return MaybeAddStreamToWritableStreamList(std::move(stream),
                                                  enqueue_result.value());
      };

  return GRPC_LATENT_SEE_PROMISE(
      "Ph2CallOutboundLoop",
      Seq(Map(HandleMetadataAndMessages(stream),
              [stream = stream.get()](absl::Status&& status) {
                GRPC_HTTP2_SERVER_DLOG
                    << "Http2ServerTransport::CallOutboundLoop "
                       "Completed initial metadata and messages with status: "
                    << status;
                return Empty{};
              }),
          [stream, send_trailing_metadata =
                       std::move(send_trailing_metadata)]() mutable {
            return Map(
                stream->GetCallInitiator().PullServerTrailingMetadata(),
                [send_trailing_metadata = std::move(send_trailing_metadata)](
                    ServerMetadataHandle&& metadata) mutable {
                  GRPC_HTTP2_SERVER_DLOG
                      << "Http2ServerTransport::CallOutboundLoop "
                         "Received Server Trailing Metadata";
                  return send_trailing_metadata(std::move(metadata));
                });
          }));
}

absl::Status Http2ServerTransport::InitializeStream(
    GRPC_UNUSED Stream& stream) {
  GRPC_DCHECK(false) << "Should not be called for server";
  return absl::OkStatus();
}

std::optional<RefCountedPtr<Stream>> Http2ServerTransport::MakeStream(
    CallInitiator&& call_initiator, const uint32_t stream_id) {
  RefCountedPtr<Stream> stream =
      MakeRefCounted<Stream>(call_initiator, flow_control_, stream_id,
                             settings_->peer().allow_true_binary_metadata());
  const bool on_done_added = SetOnDone(stream);
  if (!on_done_added) return std::nullopt;
  return std::move(stream);
}

Http2Status Http2ServerTransport::IncomingStream(
    ClientMetadataHandle&& metadata, const uint32_t stream_id) {
  {
    MutexLock lock(&transport_mutex_);
    if (is_transport_closed_) {
      return Http2Status::Http2ConnectionError(Http2ErrorCode::kRefusedStream,
                                               "Transport is closed.");
    }
  }

  GRPC_DCHECK(LookupStream(stream_id) == nullptr);

  // TODO(tjagtap) : [PH2][P1] : Evaluate use of
  // SimpleArenaAllocator vs CallArenaAllocator here.
  RefCountedPtr<Arena> arena = SimpleArenaAllocator(0)->MakeArena();
  arena->SetContext<EventEngine>(event_engine_.get());
  CallInitiatorAndHandler call =
      MakeCallPair(std::move(metadata), std::move(arena));

  // TODO(akshitpatel) : [PH2][P2] : For the server side, MakeStream most likely
  // will not fail. Evaluate this.
  std::optional<RefCountedPtr<Stream>> result =
      MakeStream(std::move(call.initiator), stream_id);
  if (!result.has_value()) {
    return Http2Status::Http2StreamError(
        Http2ErrorCode::kInternalError,
        std::string(GrpcErrors::kStreamCreationFailed));
  }
  RefCountedPtr<Stream> stream = std::move(result.value());
  AddToStreamList(stream);
  stream->SetInitialMetadataReceived();

  stream->GetCallInitiator().SpawnGuarded(
      "CallOutboundLoop",
      [self = RefAsSubclass<Http2ServerTransport>(), stream = std::move(stream),
       call_handler = std::move(call.handler)]() mutable {
        self->call_destination_->StartCall(std::move(call_handler));
        return Map(self->CallOutboundLoop(std::move(stream)),
                   [self](absl::Status status) { return status; });
      });
  return Http2Status::Ok();
}

void Http2ServerTransport::BeginCloseStream(
    RefCountedPtr<Stream> stream, uint32_t reset_stream_error_code,
    absl::Status trailing_metadata_status, DebugLocation whence) {
  GRPC_DCHECK(!trailing_metadata_status.ok());
  if (stream == nullptr) {
    GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::BeginCloseStream stream"
                              "is null reset_stream_error_code="
                           << reset_stream_error_code
                           << " status=" << trailing_metadata_status;
    return;
  }

  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::BeginCloseStream for stream id: "
      << stream->GetStreamId() << " error_code=" << reset_stream_error_code
      << " Status=" << trailing_metadata_status << " location=" << whence.file()
      << ":" << whence.line();

  // Enqueue RST_STREAM.
  absl::StatusOr<StreamWritabilityUpdate> enqueue_result =
      stream->EnqueueResetStream(reset_stream_error_code);
  GRPC_HTTP2_SERVER_DLOG << "Enqueued ResetStream with error code="
                         << reset_stream_error_code
                         << " status=" << enqueue_result.status();
  if (enqueue_result.ok()) {
    GRPC_UNUSED absl::Status status =
        MaybeAddStreamToWritableStreamList(stream, enqueue_result.value());
  }
  HandleStreamStateChange(
      *stream, stream->OnInitiateReset(std::move(trailing_metadata_status)));
  read_context_.OnResetFrameEnqueued(reset_stream_error_code);
}

void Http2ServerTransport::HandleStreamStateChange(Stream& stream,
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
      if (result.GetType() == Http2Status::Http2ErrorType::kConnectionError) {
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::HandleStreamStateChange (DiscardHeaders) "
               "for stream id: "
            << stream.GetStreamId()
            << " failed to partially process header : " << result.DebugString();
        GRPC_UNUSED absl::Status status =
            HandleError(/*stream=*/nullptr, std::move(result));
        return;
      }
    }
  }
  if (change.stream_became_closed) {
    CleanupStream(stream);
  }
}

void Http2ServerTransport::CleanupStream(Stream& stream) {
  bool should_close = false;
  {
    MutexLock lock(&transport_mutex_);
    stream_list_.erase(stream.GetStreamId());
    // Close transport if graceful GOAWAY has been sent and there are no more
    // streams.
    if (goaway_manager_.IsFinalGracefulGoawaySent() && stream_list_.empty()) {
      should_close = true;
    }
  }
  if (should_close) {
    MaybeSpawnCloseTransport(Http2Status::AbslConnectionError(
        absl::StatusCode::kUnavailable, "Graceful shutdown complete."));
  }
}

absl::Status Http2ServerTransport::UpdateAllStreamsWritability() {
  MutexLock lock(&transport_mutex_);
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::UpdateAllStreamsWritability total streams: "
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
      GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::"
                                "UpdateAllStreamsWritability failed for stream "
                             << stream_id << " with status " << status;
      return status;
    }
  }

  return absl::OkStatus();
}

//////////////////////////////////////////////////////////////////////////////
// Ping Keepalive and Goaway

void Http2ServerTransport::MaybeSpawnPingTimeout(
    std::optional<uint64_t> opaque_data) {
  if (opaque_data.has_value()) {
    SpawnGuardedTransportParty(
        "PingTimeout", [self = RefAsSubclass<Http2ServerTransport>(),
                        opaque_data = *opaque_data]() {
          return self->ping_manager_->TimeoutPromise(opaque_data);
        });
  }
}
void Http2ServerTransport::MaybeSpawnDelayedPing(
    std::optional<Duration> delayed_ping_wait) {
  if (delayed_ping_wait.has_value()) {
    SpawnGuardedTransportParty(
        "DelayedPing", [self = RefAsSubclass<Http2ServerTransport>(),
                        wait = *delayed_ping_wait]() {
          GRPC_HTTP2_PING_LOG << "Scheduling delayed ping after wait=" << wait;
          return self->ping_manager_->DelayedPingPromise(wait);
        });
  }
}

absl::Status Http2ServerTransport::AckPing(uint64_t opaque_data) {
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
    GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::AckPing Unknown ping "
                              "response received for ping id="
                           << opaque_data;
  }

  return absl::OkStatus();
}

void Http2ServerTransport::MaybeSpawnKeepaliveLoop() {
  if (keepalive_manager_->IsKeepAliveLoopNeeded()) {
    SpawnGuardedTransportParty(
        "KeepaliveLoop", [self = RefAsSubclass<Http2ServerTransport>()]() {
          return self->keepalive_manager_->KeepaliveLoop();
        });
  }
}

auto Http2ServerTransport::SpawnGracefulGoawayPromise(Slice&& debug_data) {
  SpawnGuardedTransportParty(
      "GracefulGoaway",
      [self = RefAsSubclass<Http2ServerTransport>(),
       debug_data = std::forward<Slice>(debug_data)]() mutable {
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::SpawnGracefulGoawayPromise: "
               "Initiated graceful GOAWAY";
        return self->UntilTransportClosed(Map(
            self->goaway_manager_.RequestGoaway(
                Http2ErrorCode::kNoError, std::move(debug_data),
                self->last_incoming_stream_id_, /*immediate=*/false),
            [self](absl::Status status) {
              bool should_close = false;
              {
                MutexLock lock(&self->transport_mutex_);
                if (self->GetActiveStreamCountLocked() == 0) {
                  should_close = true;
                }
              }
              if (should_close) {
                self->MaybeSpawnCloseTransport(Http2Status::AbslConnectionError(
                    absl::StatusCode::kUnavailable,
                    "Graceful shutdown complete."));
              }
              return status;
            }));
      });
}

//////////////////////////////////////////////////////////////////////////////
// Error Path and Close Path

absl::Status Http2ServerTransport::HandleError(RefCountedPtr<Stream> stream,
                                               Http2Status status,
                                               DebugLocation whence) {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::HandleError for stream id="
                         << (stream != nullptr
                                 ? absl::StrCat(stream->GetStreamId())
                                 : "nullopt")
                         << " status=" << status.DebugString()
                         << " location=" << whence.file() << ":"
                         << whence.line();
  Http2Status::Http2ErrorType error_type = status.GetType();
  GRPC_DCHECK(error_type != Http2Status::Http2ErrorType::kOk);

  if (error_type == Http2Status::Http2ErrorType::kConnectionError) {
    GRPC_DCHECK(stream == nullptr);
    absl::Status absl_status = status.GetAbslConnectionError();
    MaybeSpawnCloseTransport(std::move(status), whence);
    return absl_status;
  } else if (error_type == Http2Status::Http2ErrorType::kStreamError) {
    uint32_t reset_stream_error_code =
        Http2ErrorCodeToFrameErrorCode(status.GetStreamErrorCode());
    if (stream != nullptr) {
      BeginCloseStream(std::move(stream), reset_stream_error_code,
                       status.GetAbslStreamError(), whence);
    }
    return absl::OkStatus();
  }

  GPR_UNREACHABLE_CODE(return absl::InternalError("Invalid error type"));
}

void Http2ServerTransport::MaybeSpawnCloseTransport(Http2Status http2_status,
                                                    DebugLocation whence) {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::MaybeSpawnCloseTransport "
                            "status="
                         << http2_status.DebugString()
                         << " location=" << whence.file() << ":"
                         << whence.line();

  ReleasableMutexLock lock(&transport_mutex_);
  if (is_transport_closed_) {
    lock.Release();
    return;
  }
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::MaybeSpawnCloseTransport "
                            "Initiating transport close";
  is_transport_closed_ = true;
  absl::flat_hash_map<uint32_t, RefCountedPtr<Stream>> stream_list =
      std::move(stream_list_);
  stream_list_.clear();
  ReportDisconnectionLocked(
      GRPC_CHANNEL_SHUTDOWN, http2_status.GetAbslConnectionError(), {},
      absl::StrCat("Transport closed: ", http2_status.DebugString()).c_str());
  lock.Release();

  SpawnInfallibleTransportParty(
      "CloseTransport",
      [self = RefAsSubclass<Http2ServerTransport>(),
       stream_list = std::move(stream_list),
       http2_status = std::move(http2_status), whence]() mutable {
        self->security_frame_handler_->OnTransportClosed();
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::MaybeSpawnCloseTransport "
               "Cleaning up call stacks";
        // Clean up the call stacks for all active streams.
        for (const auto& pair : stream_list) {
          RefCountedPtr<Stream> stream = pair.second;
          self->BeginCloseStream(std::move(stream),
                                 Http2ErrorCodeToFrameErrorCode(
                                     http2_status.GetConnectionErrorCode()),
                                 http2_status.GetAbslConnectionError(), whence);
        }

        // Sleep for kGoawaySendTimeoutSeconds before closing the transport to
        // let write buffers drain.
        return Map(
            Race(AssertResultType<absl::Status>(
                     self->goaway_manager_.RequestGoaway(
                         http2_status.GetConnectionErrorCode(),
                         Slice::FromCopiedString(std::string(
                             http2_status.GetAbslConnectionError().message())),
                         self->last_incoming_stream_id_, /*immediate=*/true)),
                 Sleep(Duration::Seconds(kGoawaySendTimeoutSeconds))),
            [self](auto) mutable {
              self->CloseTransport();
              return Empty{};
            });
      });
}

void Http2ServerTransport::CloseTransport() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::CloseTransport";

  transport_closed_latch_.Set();
  settings_->HandleTransportShutdown(event_engine_.get());

  if (on_close_callback_ != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, on_close_callback_, absl::OkStatus());
    on_close_callback_ = nullptr;
  }

  general_party_.reset();
}

//////////////////////////////////////////////////////////////////////////////
// Misc Transport Stuff

void Http2ServerTransport::ReportDisconnection(
    const grpc_connectivity_state state, const absl::Status& status,
    StateWatcher::DisconnectInfo disconnect_info, const char* reason) {
  MutexLock lock(&transport_mutex_);
  ReportDisconnectionLocked(state, status, disconnect_info, reason);
}

void Http2ServerTransport::ReportDisconnectionLocked(
    const grpc_connectivity_state state, const absl::Status& status,
    StateWatcher::DisconnectInfo disconnect_info, const char* reason) {
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ReportDisconnectionLocked status="
      << status.ToString() << "; reason=" << reason;
  state_tracker_.SetState(state, status, reason);
  NotifyStateWatcherOnDisconnectLocked(status, disconnect_info);
}

bool Http2ServerTransport::SetOnDone(RefCountedPtr<Stream> stream) {
  // TODO(akshitpatel) : [PH2][P0] : Implement this.
  return stream->GetCallInitiator().OnDone(
      [self = RefAsSubclass<Http2ServerTransport>(),
       stream = std::move(stream)](GRPC_UNUSED bool cancelled) mutable {});
}
//   return call_handler.OnDone([self = RefAsSubclass<Http2ServerTransport>(),
//                               stream =
//                                   std::move(stream)](bool cancelled) mutable
//                                   {
//     GRPC_HTTP2_SERVER_DLOG << "PH2: Client call " << self.get()
//                            << " id=" << stream->GetStreamId()
//                            << " done: cancelled=" << cancelled;
//     absl::StatusOr<StreamWritabilityUpdate> enqueue_result;
//     GRPC_HTTP2_SERVER_DLOG << "PH2: Client call " << self.get()
//                            << " id=" << stream->GetStreamId()
//                            << " done: stream=" << stream.get()
//                            << " cancelled=" << cancelled;

//     // If the stream is already closed for writes, then we don't need to
//     // enqueue the reset stream or the half closed frame.
//     if (stream->IsClosedForWrites()) {
//       GRPC_HTTP2_SERVER_DLOG << "PH2: Client call " << self.get()
//                              << " id=" << stream->GetStreamId()
//                              << " done: stream already closed for writes";
//       return;
//     }

//     if (cancelled) {
//       // In most of the cases, EnqueueResetStream would be a no-op as
//       // BeginCloseStream would have already enqueued the reset stream.
//       // Currently only Aborts from application will actually enqueue
//       // the reset stream here.
//       enqueue_result = stream->EnqueueResetStream(
//           static_cast<uint32_t>(Http2ErrorCode::kCancel));
//       GRPC_HTTP2_SERVER_DLOG << "Enqueued ResetStream with error code="
//                              <<
//                              static_cast<uint32_t>(Http2ErrorCode::kCancel)
//                              << " status=" << enqueue_result.status();
//     } else {
//       enqueue_result = stream->EnqueueHalfClosed();
//       GRPC_HTTP2_SERVER_DLOG << "Enqueued HalfClosed with result="
//                              << enqueue_result.status();
//     }

//     if (GPR_LIKELY(enqueue_result.ok())) {
//       GRPC_HTTP2_SERVER_DLOG
//           << "Http2ServerTransport::SetOnDone "
//              "MaybeAddStreamToWritableStreamList for stream= "
//           << stream->GetStreamId() << " enqueue_result={became_writable="
//           << enqueue_result.value().became_writable << ", priority="
//           << static_cast<uint8_t>(enqueue_result.value().priority) << "}";
//       GRPC_UNUSED absl::Status status =
//           self->MaybeAddStreamToWritableStreamList(std::move(stream),
//                                                    enqueue_result.value());
//     }
//   });
// }

void Http2ServerTransport::ReadChannelArgs(const ChannelArgs& channel_args,
                                           TransportChannelArgs& args) {
  http2::ReadChannelArgs(channel_args, args, settings_->mutable_local(),
                         flow_control_,
                         /*is_client=*/kIsClient);

  // Assign the channel args to the member variables.
  keepalive_time_ = args.keepalive_time;
  read_context_.set_soft_limit(args.max_header_list_size_soft_limit);
  keepalive_permit_without_calls_ = args.keepalive_permit_without_calls;
  test_only_ack_pings_ = args.test_only_ack_pings;

  settings_->SetSettingsTimeout(args.settings_timeout);
  if (args.max_usable_hpack_table_size >= 0) {
    encoder_.SetMaxUsableSize(args.max_usable_hpack_table_size);
  }
}

//////////////////////////////////////////////////////////////////////////////
// Inner Classes and Structs

std::unique_ptr<PingInterface>
Http2ServerTransport::PingSystemInterfaceImpl::Make(
    Http2ServerTransport* transport) {
  return std::make_unique<PingSystemInterfaceImpl>(
      PingSystemInterfaceImpl(transport));
}

absl::Status Http2ServerTransport::PingSystemInterfaceImpl::TriggerWrite() {
  return transport_->TriggerWriteCycle();
}

Promise<absl::Status>
Http2ServerTransport::PingSystemInterfaceImpl::PingTimeout() {
  GRPC_HTTP2_SERVER_DLOG << "PingSystemInterfaceImpl::PingTimeout at time: "
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

std::unique_ptr<KeepAliveInterface>
Http2ServerTransport::KeepAliveInterfaceImpl::Make(
    Http2ServerTransport* transport) {
  return std::make_unique<KeepAliveInterfaceImpl>(
      KeepAliveInterfaceImpl(transport));
}

Promise<absl::Status>
Http2ServerTransport::KeepAliveInterfaceImpl::SendPingAndWaitForAck() {
  return TrySeq(
      [transport = transport_] { return transport->TriggerWriteCycle(); },
      [transport = transport_] { return transport->WaitForPingAck(); });
}

Promise<absl::Status>
Http2ServerTransport::KeepAliveInterfaceImpl::OnKeepAliveTimeout() {
  GRPC_HTTP2_SERVER_DLOG
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

bool Http2ServerTransport::KeepAliveInterfaceImpl::NeedToSendKeepAlivePing() {
  bool need_to_send_ping = false;
  {
    MutexLock lock(&transport_->transport_mutex_);
    need_to_send_ping = (transport_->keepalive_permit_without_calls_ ||
                         transport_->GetActiveStreamCountLocked() > 0);
  }
  return need_to_send_ping;
}

std::unique_ptr<GoawayInterface>
Http2ServerTransport::GoawayInterfaceImpl::Make(
    Http2ServerTransport* transport) {
  return std::make_unique<GoawayInterfaceImpl>(GoawayInterfaceImpl(transport));
}

uint32_t Http2ServerTransport::GoawayInterfaceImpl::GetLastAcceptedStreamId() {
  return transport_->last_incoming_stream_id_;
}

//////////////////////////////////////////////////////////////////////////////
// Constructor, Destructor etc.

Http2ServerTransport::Http2ServerTransport(
    PromiseEndpoint endpoint, const ChannelArgs& channel_args,
    std::shared_ptr<EventEngine> event_engine,
    absl::AnyInvocable<void(absl::StatusOr<uint32_t>)> on_receive_settings,
    grpc_closure* on_close_callback)
    : channelz::DataSource(http2::CreateChannelzSocketNode(
          endpoint.GetEventEngineEndpoint(), channel_args)),
      event_engine_(std::move(event_engine)),
      endpoint_(std::move(endpoint)),
      settings_(MakeRefCounted<SettingsPromiseManager>(
          kIsClient, std::move(on_receive_settings))),
      on_close_callback_(on_close_callback),
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
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Constructor Begin";

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

  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Constructor End";
}

Http2ServerTransport::~Http2ServerTransport() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Destructor Begin";
  // GRPC_DCHECK(stream_list_.empty());
  // GRPC_DCHECK(general_party_ == nullptr);
  // memory_owner_.Reset();

  // TODO(akshitpatel) : [PH2][P0][Close] : Remove call to
  // HandleTransportShutdown() from here and plumb CloseTransport() correctly.
  settings_->HandleTransportShutdown(event_engine_.get());
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Destructor End";
}

//////////////////////////////////////////////////////////////////////////////
// Transport Functions

void Http2ServerTransport::SetCallDestination(
    RefCountedPtr<UnstartedCallDestination> unstarted_call_destination) {
  // This is called once in the lifetime of the transport.
  GRPC_CHECK(call_destination_ == nullptr);
  GRPC_CHECK(unstarted_call_destination != nullptr);
  call_destination_ = std::move(unstarted_call_destination);
  InitializeAndSpawnTransportLoops();
}

void Http2ServerTransport::PerformOp(grpc_transport_op* op) {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport PerformOp Begin";
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
  if (!op->disconnect_with_error.ok()) {
    MaybeSpawnCloseTransport(Http2Status::Http2ConnectionError(
        AbslStatusCodeToErrorCode(op->disconnect_with_error.code()),
        std::string(op->disconnect_with_error.message())));
    did_stuff = true;
  }
  // We always consider this case as a graceful shutdown.
  if (!op->goaway_error.ok()) {
    GRPC_HTTP2_SERVER_DLOG << "GracefulGoaway triggered with error: "
                           << op->goaway_error;
    SpawnGracefulGoawayPromise(
        Slice::FromCopiedString(op->goaway_error.message()));
    did_stuff = true;
  }
  GRPC_DCHECK(did_stuff) << "Unimplemented transport perform op ";

  ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());

  // TODO(tjagtap) : [PH2][P2] :
  // Refer src/core/ext/transport/chttp2/transport/chttp2_transport.cc
  // perform_transport_op_locked
  // Maybe more operations needed to be implemented.
  // TODO(tjagtap) : [PH2][P2] : Consider either not using a transport level
  // lock, or making this run on the Transport party - whatever is better.
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport PerformOp End";
}

void Http2ServerTransport::Orphan() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::Orphan Begin";
  SourceDestructing();
  MaybeSpawnCloseTransport(
      ToHttpOkOrConnError(absl::UnavailableError("Orphaned")));
  Unref();
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::Orphan End";
}

void Http2ServerTransport::SpawnTransportLoops() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::SpawnTransportLoops Begin";
  MaybeSpawnKeepaliveLoop();

  // SpawnGuardedTransportParty(
  //     "FlowControlPeriodicUpdateLoop",
  //     UntilTransportClosed(FlowControlPeriodicUpdateLoop()));

  if (!TriggerWriteCycleOrHandleError()) {
    return;
  }
  // For Client, write happens before read. So MultiplexerLoop is spawned first.
  // ReadLoop is spawned after the first write.
  // For Server, read happens before write. So ReadLoop is spawned first.
  SpawnGuardedTransportParty("ReadLoop", UntilTransportClosed(ReadLoop()));
  SpawnGuardedTransportParty("MultiplexerLoop",
                             UntilTransportClosed(MultiplexerLoop()));

  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::SpawnTransportLoops End";
}

void Http2ServerTransport::InitializeAndSpawnTransportLoops() {
  SpawnGuardedTransportParty(
      "SpawnTransportLoops", [self = RefAsSubclass<Http2ServerTransport>()] {
        return Map(self->EndpointReadSlice(GRPC_CHTTP2_CLIENT_CONNECT_STRLEN),
                   [self](absl::StatusOr<Slice> status) -> absl::Status {
                     Http2Status result =
                         ValidateIncomingConnectionPreface(status);
                     if (!result.IsOk()) {
                       return self->HandleError(nullptr, std::move(result));
                     }
                     self->SpawnTransportLoops();
                     return absl::OkStatus();
                   });
      });
}

}  // namespace http2
}  // namespace grpc_core
