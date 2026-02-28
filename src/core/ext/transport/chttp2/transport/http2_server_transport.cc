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
#include "src/core/ext/transport/chttp2/transport/incoming_metadata_tracker.h"
#include "src/core/ext/transport/chttp2/transport/keepalive.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
#include "src/core/ext/transport/chttp2/transport/ping_promise.h"
#include "src/core/ext/transport/chttp2/transport/security_frame.h"
#include "src/core/ext/transport/chttp2/transport/stream.h"
#include "src/core/ext/transport/chttp2/transport/stream_data_queue.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace_impl.h"
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
// TODO(tjagtap) : [PH2][P3] : Delete this comment when http2
// rollout begins

constexpr int kIsClient = false;

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
  return stream->flow_control.remote_window_delta();
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

  // TODO(akshitpatel) : [PH2][P3] : Investigate if we should do this even if
  // the function returns a non-ok status?
  //   ping_manager_->ReceivedDataFrame();

  //   RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);

  //   ValueOrHttp2Status<chttp2::FlowControlAction> flow_control_action =
  //       ProcessIncomingDataFrameFlowControl(current_frame_header_,
  //       flow_control_,
  //                                           stream.get());
  //   if (!flow_control_action.IsOk()) {
  //     return ValueOrHttp2Status<chttp2::FlowControlAction>::TakeStatus(
  //         std::move(flow_control_action));
  //   }
  //   ActOnFlowControlAction(flow_control_action.value(), stream.get());

  //   if (stream == nullptr) {
  //     // TODO(tjagtap) : [PH2][P2] : Implement the correct behaviour later.
  //     // RFC9113 : If a DATA frame is received whose stream is not in the
  //     "open"
  //     // or "half-closed (local)" state, the recipient MUST respond with a
  //     stream
  //     // error (Section 5.4.2) of type STREAM_CLOSED.
  //     GRPC_HTTP2_SERVER_DLOG
  //         << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) {
  //         stream_id="
  //         << frame.stream_id << "} Lookup Failed";
  //     return Http2Status::Ok();
  //   }

  //   // TODO(akshitpatel) : [PH2][P3] : We should add a check to reset stream
  //   if
  //   // the stream state is kIdle as well.

  //   Http2Status stream_status = stream->CanStreamReceiveDataFrames();
  //   if (!stream_status.IsOk()) {
  //     return stream_status;
  //   }

  //   // Add frame to assembler
  //   GRPC_HTTP2_SERVER_DLOG
  //       << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) "
  //          "AppendNewDataFrame";
  //   GrpcMessageAssembler& assembler = stream->assembler;
  //   Http2Status status =
  //       assembler.AppendNewDataFrame(frame.payload, frame.end_stream);
  //   if (!status.IsOk()) {
  //     GRPC_HTTP2_SERVER_DLOG
  //         << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) "
  //            "AppendNewDataFrame Failed";
  //     return status;
  //   }

  //   // Pass the messages up the stack if it is ready.
  //   while (true) {
  //     GRPC_HTTP2_SERVER_DLOG
  //         << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) "
  //            "ExtractMessage";
  //     ValueOrHttp2Status<MessageHandle> result = assembler.ExtractMessage();
  //     if (!result.IsOk()) {
  //       GRPC_HTTP2_SERVER_DLOG
  //           << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) "
  //              "ExtractMessage Failed";
  //       return
  //       ValueOrHttp2Status<MessageHandle>::TakeStatus(std::move(result));
  //     }
  //     MessageHandle message = TakeValue(std::move(result));
  //     if (message != nullptr) {
  //       GRPC_HTTP2_SERVER_DLOG
  //           << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) "
  //              "SpawnPushMessage "
  //           << message->DebugString();
  //       stream->call.SpawnPushMessage(std::move(message));
  //       continue;
  //     }
  //     GRPC_HTTP2_SERVER_DLOG
  //         << "Http2ServerTransport::ProcessIncomingFrame(DataFrame) While
  //         Break";
  //     break;
  //   }

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

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    Http2HeaderFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-headers
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(HeaderFrame) {stream_id = "
      << frame.stream_id << ",end_headers = " << frame.end_headers
      << ", end_stream=" << frame.end_stream << " }";
  // State update MUST happen before processing the frame.
  //   incoming_headers_.OnHeaderReceived(frame);

  //   ping_manager_->ReceivedDataFrame();

  //   RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
  //   if (stream == nullptr) {
  //     // TODO(tjagtap) : [PH2][P3] : Implement this.
  //     // RFC9113 : The identifier of a newly established stream MUST be
  //     // numerically greater than all streams that the initiating endpoint
  //     has
  //     // opened or reserved. This governs streams that are opened using a
  //     HEADERS
  //     // frame and streams that are reserved using PUSH_PROMISE. An endpoint
  //     that
  //     // receives an unexpected stream identifier MUST respond with a
  //     connection
  //     // error (Section 5.4.1) of type PROTOCOL_ERROR.
  //     GRPC_HTTP2_SERVER_DLOG
  //         << "Http2ServerTransport::ProcessIncomingFrame(HeaderFrame) { "
  //            "stream_id="
  //         << frame.stream_id << "} Lookup Failed";
  //     return ParseAndDiscardHeaders(std::move(frame.payload),
  //     frame.end_headers,
  //                                   /*stream=*/nullptr, Http2Status::Ok());
  //   }

  //   if (stream->IsStreamHalfClosedRemote()) {
  //     return ParseAndDiscardHeaders(
  //         std::move(frame.payload), frame.end_headers, stream.get(),
  //         Http2Status::Http2StreamError(
  //             Http2ErrorCode::kStreamClosed,
  //             std::string(RFC9113::kHalfClosedRemoteState)));
  //   }

  //   if (incoming_headers_.ClientReceivedDuplicateMetadata(
  //           stream->did_receive_initial_metadata,
  //           stream->did_receive_trailing_metadata)) {
  //     return ParseAndDiscardHeaders(
  //         std::move(frame.payload), frame.end_headers, stream.get(),
  //         Http2Status::Http2StreamError(
  //             Http2ErrorCode::kInternalError,
  //             std::string(GrpcErrors::kTooManyMetadata)));
  //   }

  //   Http2Status append_result =
  //   stream->header_assembler.AppendHeaderFrame(frame); if
  //   (!append_result.IsOk()) {
  //     // Frame payload is not consumed if AppendHeaderFrame returns a non-OK
  //     // status. We need to process it to keep our in consistent state.
  //     return ParseAndDiscardHeaders(std::move(frame.payload),
  //     frame.end_headers,
  //                                   stream.get(), std::move(append_result));
  //   }

  //   Http2Status status = ProcessMetadata(stream);
  //   if (!status.IsOk()) {
  //     // Frame payload has been moved to the HeaderAssembler. So calling
  //     // ParseAndDiscardHeaders with an empty buffer.
  //     return ParseAndDiscardHeaders(SliceBuffer(), frame.end_headers,
  //                                   stream.get(), std::move(status));
  //   }

  // Frame payload has either been processed or moved to the HeaderAssembler.
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    Http2RstStreamFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-rst_stream
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(RstStreamFrame) { "
         "stream_id="
      << frame.stream_id << ", error_code=" << frame.error_code << " }";

  //   Http2ErrorCode error_code =
  //   FrameErrorCodeToHttp2ErrorCode(frame.error_code); absl::Status status =
  //   absl::Status(ErrorCodeToAbslStatusCode(error_code),
  //                                      "Reset stream frame received.");
  //   RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
  //   if (stream != nullptr) {
  //     stream->MarkHalfClosedRemote();
  //     BeginCloseStream(std::move(stream),
  //                      /*reset_stream_error_code=*/std::nullopt,
  //                      CancelledServerMetadataFromStatus(status));
  //   }

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

  //   if (!frame.ack) {
  //     Http2Status status = ValidateSettingsValues(frame.settings);
  //     if (!status.IsOk()) {
  //       return status;
  //     }
  //     settings_->BufferPeerSettings(std::move(frame.settings));
  //     absl::Status trigger_write_status = TriggerWriteCycle();
  //     if (!trigger_write_status.ok()) {
  //       return ToHttpOkOrConnError(trigger_write_status);
  //     }

  //     if (GPR_UNLIKELY(!settings_->IsFirstPeerSettingsApplied())) {
  //       // Apply the first settings before we read any other frames.
  //       reader_state_.SetPauseReadLoop();
  //     }
  //   } else {
  //     if (settings_->OnSettingsAckReceived()) {
  //       parser_.hpack_table()->SetMaxBytes(
  //           settings_->acked().header_table_size());
  //       ActOnFlowControlAction(flow_control_.SetAckedInitialWindow(
  //                                  settings_->acked().initial_window_size()),
  //                              /*stream=*/nullptr);
  //     } else {
  //       // TODO(tjagtap) [PH2][P4] : The RFC does not say anything about
  //       what
  //       // should happen if we receive an unsolicited SETTINGS ACK. Decide
  //       if we
  //       // want to respond with any error or just proceed.
  //       LOG(ERROR) << "Settings ack received without sending settings";
  //     }
  //   }
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(Http2PingFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-ping
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(PingFrame) { ack="
      << frame.ack << ", opaque=" << frame.opaque << " }";
  //   if (frame.ack) {
  //     return ToHttpOkOrConnError(AckPing(frame.opaque));
  //   } else {
  //     if (test_only_ack_pings_) {
  //       // TODO(akshitpatel) : [PH2][P2] : Have a counter to track number
  //       // of pending induced frames (Ping/Settings Ack). This is to
  //       // ensure that if write is taking a long time, we can stop reads
  //       // and prioritize writes. RFC9113: PING responses SHOULD be given
  //       // higher priority than any other frame.
  //       ping_manager_->AddPendingPingAck(frame.opaque);
  //       // TODO(akshitpatel) : [PH2][P2] : This is done assuming that the
  //       // other ProcessFrame promises may return stream or connection
  //       // failures. If this does not turn out to be true, consider
  //       // returning absl::Status here.
  //       return ToHttpOkOrConnError(TriggerWriteCycle());
  //     } else {
  //       GRPC_HTTP2_SERVER_DLOG
  //           << "Http2ServerTransport::ProcessIncomingFrame(PingFrame) "
  //              "test_only_ack_pings_ is false. Ignoring the ping request.";
  //     }
  //   }
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

  //   RefCountedPtr<Stream> stream = nullptr;
  //   if (frame.stream_id != 0) {
  //     stream = LookupStream(frame.stream_id);
  //   }
  //   if (stream != nullptr) {
  //     StreamWritabilityUpdate update =
  //         stream->ReceivedFlowControlWindowUpdate(frame.increment);
  //     if (update.became_writable) {
  //       absl::Status status = writable_stream_list_.EnqueueWrapper(
  //           stream, update.priority,
  //           AreTransportFlowControlTokensAvailable());
  //       if (!status.ok()) {
  //         return ToHttpOkOrConnError(status);
  //       }
  //     }
  //   }

  //   const bool should_trigger_write =
  //   ProcessIncomingWindowUpdateFrameFlowControl(
  //       frame, flow_control_, stream.get());
  //   if (should_trigger_write) {
  //     return ToHttpOkOrConnError(TriggerWriteCycle());
  //   }
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    Http2ContinuationFrame&& frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-continuation
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(ContinuationFrame) { "
         "stream_id="
      << frame.stream_id << ", end_headers=" << frame.end_headers << " }";

  //   // State update MUST happen before processing the frame.
  //   incoming_headers_.OnContinuationReceived(frame);

  //   RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
  //   if (stream == nullptr) {
  //     // TODO(tjagtap) : [PH2][P3] : Implement this.
  //     // RFC9113 : The identifier of a newly established stream MUST be
  //     // numerically greater than all streams that the initiating endpoint
  //     has
  //     // opened or reserved. This governs streams that are opened using a
  //     HEADERS
  //     // frame and streams that are reserved using PUSH_PROMISE. An
  //     endpoint that
  //     // receives an unexpected stream identifier MUST respond with a
  //     connection
  //     // error (Section 5.4.1) of type PROTOCOL_ERROR.
  //     return ParseAndDiscardHeaders(std::move(frame.payload),
  //     frame.end_headers,
  //                                   nullptr, Http2Status::Ok());
  //   }

  //   if (stream->IsStreamHalfClosedRemote()) {
  //     return ParseAndDiscardHeaders(
  //         std::move(frame.payload), frame.end_headers, stream.get(),
  //         Http2Status::Http2StreamError(
  //             Http2ErrorCode::kStreamClosed,
  //             std::string(RFC9113::kHalfClosedRemoteState)));
  //   }

  //   Http2Status append_result =
  //       stream->header_assembler.AppendContinuationFrame(frame);
  //   if (!append_result.IsOk()) {
  //     // Frame payload is not consumed if AppendContinuationFrame returns a
  //     // non-OK status. We need to process it to keep our in consistent
  //     state. return ParseAndDiscardHeaders(std::move(frame.payload),
  //     frame.end_headers,
  //                                   stream.get(),
  //                                   std::move(append_result));
  //   }

  //   Http2Status status = ProcessMetadata(stream);
  //   if (!status.IsOk()) {
  //     // Frame payload is consumed by HeaderAssembler. So passing an empty
  //     // SliceBuffer to ParseAndDiscardHeaders.
  //     return ParseAndDiscardHeaders(SliceBuffer(), frame.end_headers,
  //                                   stream.get(), std::move(status));
  //   }

  // Frame payload has either been processed or moved to the HeaderAssembler.
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    Http2SecurityFrame&& frame) {
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ProcessIncomingFrame(SecurityFrame) ";
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

Http2Status Http2ServerTransport::ProcessMetadata(
    RefCountedPtr<Stream> stream) {
  HeaderAssembler& assembler = stream->header_assembler;
  CallHandler call = stream->call;

  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::ProcessMetadata";
  if (assembler.IsReady()) {
    ValueOrHttp2Status<ServerMetadataHandle> read_result =
        assembler.ReadMetadata(parser_, !incoming_headers_.HeaderHasEndStream(),
                               /*max_header_list_size_soft_limit=*/
                               incoming_headers_.soft_limit(),
                               /*max_header_list_size_hard_limit=*/
                               settings_->acked().max_header_list_size());
    if (read_result.IsOk()) {
      // ServerMetadataHandle metadata = TakeValue(std::move(read_result));
      // if (incoming_headers_.HeaderHasEndStream()) {
      //   stream->MarkHalfClosedRemote();
      //   stream->did_receive_trailing_metadata = true;
      //   // BeginCloseStream(std::move(stream),
      //   //                  /*reset_stream_error_code=*/std::nullopt,
      //   //                  std::move(metadata));
      // } else {
      //   GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::ProcessMetadata "
      //                             "SpawnPushServerInitialMetadata";
      //   metadata->Set(PeerString(), incoming_headers_.peer_string());
      //   stream->did_receive_initial_metadata = true;
      //   call.SpawnPushServerInitialMetadata(std::move(metadata));
      // }
      return Http2Status::Ok();
    }
    GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::ProcessMetadata Failed";
    return ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>>::TakeStatus(
        std::move(read_result));
  }
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ParseAndDiscardHeaders(
    SliceBuffer&& buffer, const bool is_end_headers, Stream* stream,
    Http2Status&& original_status, DebugLocation whence) {
  const bool is_initial_metadata = !incoming_headers_.HeaderHasEndStream();
  const uint32_t incoming_stream_id = incoming_headers_.GetStreamId();
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ParseAndDiscardHeaders buffer size: "
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
          /*is_client=*/kIsClient,
          /*max_header_list_size_soft_limit=*/
          incoming_headers_.soft_limit(),
          /*max_header_list_size_hard_limit=*/
          settings_->acked().max_header_list_size(),
          /*stream_id=*/incoming_stream_id,
      },
      stream, std::move(original_status));
}

auto Http2ServerTransport::ReadAndProcessOneFrame() {
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ReadAndProcessOneFrame Factory";
  return AssertResultType<absl::Status>(TrySeq(
      // Fetch the first kFrameHeaderSize bytes of the Frame, these contain
      // the frame header.
      EndpointReadSlice(kFrameHeaderSize),
      // Parse the frame header.
      [](Slice header_bytes) -> Http2FrameHeader {
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::ReadAndProcessOneFrame Parse "
            << header_bytes.as_string_view();
        return Http2FrameHeader::Parse(header_bytes.begin());
      },
      // Validate the incoming frame as per the current state of the transport
      [this](Http2FrameHeader header) {
        Http2Status status = ValidateFrameHeader(
            /*max_frame_size_setting*/ settings_->acked().max_frame_size(),
            /*incoming_header_in_progress*/
            incoming_headers_.IsWaitingForContinuationFrame(),
            /*incoming_header_stream_id*/
            incoming_headers_.GetStreamId(),
            /*current_frame_header*/ header,
            /*last_stream_id=*/100,  // TODO(tjagtap) : [PH2][P0] : Fix
            /*is_client=*/kIsClient, /*is_first_settings_processed=*/
            settings_->IsFirstPeerSettingsApplied());

        if (GPR_UNLIKELY(!status.IsOk())) {
          GRPC_DCHECK(status.GetType() ==
                      Http2Status::Http2ErrorType::kConnectionError);
          return HandleError(std::nullopt, std::move(status));
        }
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::ReadAndProcessOneFrame "
               "Validated Frame Header:"
            << header.ToString();
        current_frame_header_ = header;
        return absl::OkStatus();
      },
      // Read the payload of the frame.
      [this]() {
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport::ReadAndProcessOneFrame Read Frame ";
        return AssertResultType<absl::Status>(Map(
            EndpointRead(current_frame_header_.length),
            [this](absl::StatusOr<SliceBuffer>&& payload) {
              if (GPR_UNLIKELY(!payload.ok())) {
                return payload.status();
              }
              GRPC_HTTP2_SERVER_DLOG
                  << "Http2ServerTransport::ReadAndProcessOneFrame "
                     "ParseFramePayload payload length: "
                  << payload.value().Length();
              ValueOrHttp2Status<Http2Frame> frame = ParseFramePayload(
                  current_frame_header_, TakeValue(std::move(payload)));
              if (GPR_UNLIKELY(!frame.IsOk())) {
                return HandleError(current_frame_header_.stream_id,
                                   ValueOrHttp2Status<Http2Frame>::TakeStatus(
                                       std::move(frame)));
              }
              Http2Status status =
                  ProcessOneIncomingFrame(TakeValue(std::move(frame)));
              if (GPR_UNLIKELY(!status.IsOk())) {
                return HandleError(current_frame_header_.stream_id,
                                   std::move(status));
              }
              return absl::OkStatus();
            }));
      },
      [this]() -> Poll<absl::Status> {
        return reader_state_.MaybePauseReadLoop();
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

// absl::Status Http2ServerTransport::PrepareControlFrames() {
//   FrameSender frame_sender =
//       transport_write_context_.GetWriteCycle().GetFrameSender();
//   if (transport_write_context_.IsFirstWrite()) {
//     // RFC9113: That is, the connection preface starts with the string
//     // "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n". This connection preface string
//     will
//     // be sent as part of the first write cycle. This sequence MUST be
//     followed
//     // by a SETTINGS frame, which MAY be empty.
//     settings_->MaybeGetSettingsAndSettingsAckFrames(flow_control_,
//                                                     frame_sender);
//     // TODO(tjagtap) [PH2][P2][Server] : This will be opposite for server. We
//     // must read before we write for the server. So the ReadLoop will be
//     Spawned
//     // just after the constructor, and the write loop should be spawned only
//     // after the first SETTINGS frame is completely received.
//     //
//     // Because the client is expected to write before it reads, we spawn the
//     // ReadLoop of the client only after the first write is queued.
//     SpawnGuardedTransportParty("ReadLoop", ReadLoop());
//   }

//   // Order of Control Frames is important.
//   // 1. GOAWAY - This is first because if this is the final GoAway, then we
//   may
//   //             not need to send anything else to the peer.
//   // 2. SETTINGS and SETTINGS ACK
//   // 3. PING and PING acks.
//   // 4. WINDOW_UPDATE
//   // 5. Custom gRPC security frame

//   goaway_manager_.MaybeGetSerializedGoawayFrame(frame_sender);
//   bool should_spawn_security_frame_loop = false;
//   http2::Http2ErrorCode apply_status =
//       settings_->MaybeReportAndApplyBufferedPeerSettings(
//           event_engine_.get(), should_spawn_security_frame_loop);
//   if (should_spawn_security_frame_loop) {
//     const SecurityFrameHandler::EndpointExtensionState state =
//         security_frame_handler_->Initialize(event_engine_);
//     if (state.is_set) {
//       SpawnInfallibleTransportParty("SecurityFrameLoop",
//       UntilTransportClosed(SecurityFrameLoop()));
//     }
//   }

//   if (!goaway_manager_.IsImmediateGoAway() &&
//       apply_status == http2::Http2ErrorCode::kNoError) {
//     EnforceLatestIncomingSettings();
//     settings_->MaybeGetSettingsAndSettingsAckFrames(flow_control_,
//                                                     frame_sender);
//     MaybeSpawnDelayedPing(ping_manager_->MaybeGetSerializedPingFrames(
//         frame_sender, NextAllowedPingInterval()));
//     MaybeGetWindowUpdateFrames(frame_sender);
//     security_frame_handler_->MaybeAppendSecurityFrame(frame_sender);
//   }

//   if (apply_status != http2::Http2ErrorCode::kNoError) {
//     return HandleError(std::nullopt,
//                        Http2Status::Http2ConnectionError(
//                            apply_status, "Failed to apply incoming
//                            settings"));
//   }

//   return absl::OkStatus();
// }

// auto Http2ServerTransport::MaybeWriteUrgentFrames() {
//   return AssertResultType<absl::Status>(If(
//       transport_write_context_.GetWriteCycle().CanSerializeUrgentFrames(),
//       [this]() mutable {
//         WriteCycle& write_cycle = transport_write_context_.GetWriteCycle();
//         const uint64_t buffer_length = write_cycle.GetUrgentFrameCount();
//         ztrace_collector_->Append(PromiseEndpointWriteTrace{buffer_length});
//         GRPC_HTTP2_SERVER_DLOG
//             << "Http2ServerTransport::MaybeWriteUrgentFrames frame count: "
//             << buffer_length;
//         return EndpointWrite(write_cycle.SerializeUrgentFrames(
//             WriteCycle::SerializeStats{should_reset_ping_clock_}));
//       },
//       []() { return absl::OkStatus(); }));
// }

// void Http2ServerTransport::NotifyFramesWriteDone() {
//   // Notify Control modules that we have sent the frames.
//   // All notifications are expected to be synchronous.
//   GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::NotifyFramesWriteDone";
//   reader_state_.ResumeReadLoopIfPaused();
//   MaybeSpawnPingTimeout(ping_manager_->NotifyPingSent());
//   goaway_manager_.NotifyGoawaySent();
//   MaybeSpawnWaitForSettingsTimeout();
// }

// void Http2ServerTransport::NotifyUrgentFramesWriteDone() {}

// absl::Status Http2ServerTransport::DequeueStreamFrames(
//     RefCountedPtr<Stream> stream, WriteCycle& write_cycle) {
//   // write_bytes_remaining_ is passed as an upper bound on the max
//   // number of tokens that can be dequeued to prevent dequeuing huge
//   // data frames when write_bytes_remaining_ is very low. As the
//   // available transport tokens can only range from 0 to 2^31 - 1,
//   // we are clamping the write_bytes_remaining_ to that range.
//   FrameSender frame_sender = write_cycle.GetFrameSender();
//   const uint32_t tokens = GetMaxPermittedDequeue(
//       flow_control_, stream->flow_control,
//       write_cycle.GetWriteBytesRemaining(), settings_->peer());
//   const uint32_t stream_flow_control_tokens = static_cast<uint32_t>(
//       GetStreamFlowControlTokens(stream->flow_control, settings_->peer()));
//   stream->flow_control.ReportIfStalled(
//       /*is_client=*/kIsClient, stream->GetStreamId(), settings_->peer());
//   StreamDataQueue<ClientMetadataHandle>::DequeueResult result =
//       stream->DequeueFrames(tokens, stream_flow_control_tokens,
//                             settings_->peer().max_frame_size(), encoder_,
//                             frame_sender);
//   ProcessOutgoingDataFrameFlowControl(stream->flow_control,
//                                       result.flow_control_tokens_consumed);
//   if (result.is_writable) {
//     // Stream is still writable. Enqueue it back to the writable
//     // stream list.
//     absl::Status status = writable_stream_list_.EnqueueWrapper(
//         stream, result.priority, AreTransportFlowControlTokensAvailable());

//     if (GPR_UNLIKELY(!status.ok())) {
//       GRPC_HTTP2_SERVER_DLOG
//           << "Http2ServerTransport::DequeueStreamFrames Failed to "
//              "enqueue stream "
//           << stream->GetStreamId() << " with status: " << status;
//       // Close transport if we fail to enqueue stream.
//       return HandleError(std::nullopt, ToHttpOkOrConnError(status));
//     }
//   }

//   // If the stream is aborted before initial metadata is dequeued, we will
//   // not dequeue any frames from the stream data queue (including
//   RST_STREAM).
//   // Because of this, we will add the stream to the stream_list only when
//   // we are guaranteed to send initial metadata on the wire. If the above
//   // mentioned scenario occurs, the stream ref will be dropped by the
//   // multiplexer loop as the stream will never be writable again.
//   Additionally,
//   // the other two stream refs, CallHandler OnDone and OutboundLoop will be
//   // dropped by Callv3 triggering cleaning up the stream object.
//   if (result.IsInitialMetadataDequeued()) {
//     GRPC_HTTP2_SERVER_DLOG
//         << "Http2ServerTransport::DequeueStreamFrames InitialMetadataDequeued
//         "
//            "stream_id = "
//         << stream->GetStreamId();
//     stream->SentInitialMetadata();
//     // After this point, initial metadata is guaranteed to be sent out.
//     AddToStreamList(stream);
//   }

//   if (result.IsHalfCloseDequeued()) {
//     GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::DequeueStreamFrames "
//                               "HalfCloseDequeued stream_id = "
//                            << stream->GetStreamId();
//     stream->MarkHalfClosedLocal();

//     if (stream->did_receive_trailing_metadata) {
//       CloseStream(*stream, CloseStreamArgs{/*close_reads=*/true,
//                                            /*close_writes=*/true});
//     }
//   }
//   if (result.IsResetStreamDequeued()) {
//     GRPC_HTTP2_SERVER_DLOG
//         << "Http2ServerTransport::DequeueStreamFrames ResetStreamDequeued "
//            "stream_id = "
//         << stream->GetStreamId();
//     stream->MarkHalfClosedLocal();
//     CloseStream(*stream, CloseStreamArgs{/*close_reads=*/true,
//                                          /*close_writes=*/true});
//   }

//   // Update the write_bytes_remaining_ based on the bytes consumed
//   // in the current dequeue.
//   // Note: We do tend to overestimate the bytes consumed here. This may
//   result
//   // in sending less data than target_write_size_.

//   GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::DequeueStreamFrames "
//                             "After dequeue: "
//                          << write_cycle.DebugString()
//                          << " stream_id = " << stream->GetStreamId()
//                          << " is_writable = " << result.is_writable
//                          << " stream_priority = "
//                          << static_cast<uint8_t>(result.priority);
//   return absl::OkStatus();
// }

// This MultiplexerLoop promise is responsible for Multiplexing multiple gRPC
// Requests (HTTP2 Streams) and writing them into one common endpoint.
// auto Http2ServerTransport::MultiplexerLoop() {
//   GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::MultiplexerLoop Factory";
//   return AssertResultType<absl::Status>(UntilTransportClosed(Loop([this]() {
//     return TrySeq(
//         Map(writable_stream_list_.WaitForReady(
//                 AreTransportFlowControlTokensAvailable()),
//             [this](absl::StatusOr<Empty> status) -> absl::Status {
//               if (GPR_UNLIKELY(!status.ok())) {
//                 return status.status();
//               }
//               transport_write_context_.StartWriteCycle();
//               GRPC_HTTP2_SERVER_DLOG <<
//               "Http2ServerTransport::MultiplexerLoop "
//                                         "Created WriteCycle: "
//                                      <<
//                                      transport_write_context_.DebugString();
//               return PrepareControlFrames();
//             }),
//         [this] {
//           return Map(MaybeWriteUrgentFrames(), [this](absl::Status status) {
//             if (GPR_UNLIKELY(!status.ok())) {
//               return status;
//             }
//             NotifyUrgentFramesWriteDone();
//             WriteCycle& write_cycle =
//             transport_write_context_.GetWriteCycle(); GRPC_HTTP2_SERVER_DLOG
//                 << "Http2ServerTransport::MultiplexerLoop "
//                 << "Starting to iterate over writable stream list "
//                 << write_cycle.DebugString();
//             // Drain all the writable streams till we have written
//             // max_write_size_ bytes of data or there is no more data to
//             send.
//             // In some cases, we may write more than max_write_size_
//             bytes(like
//             // writing metadata).
//             while (write_cycle.GetWriteBytesRemaining() > 0) {
//               std::optional<RefCountedPtr<Stream>> optional_stream =
//                   writable_stream_list_.ImmediateNext(
//                       AreTransportFlowControlTokensAvailable());
//               if (!optional_stream.has_value()) {
//                 GRPC_HTTP2_SERVER_DLOG
//                     << "Http2ServerTransport::MultiplexerLoop "
//                        "No writable streams available ";
//                 break;
//               }
//               RefCountedPtr<Stream> stream =
//               std::move(optional_stream.value()); GRPC_HTTP2_SERVER_DLOG
//                   << "Http2ServerTransport::MultiplexerLoop "
//                      "Next writable stream id = "
//                   << stream->GetStreamId()
//                   << " is_closed_for_writes = " <<
//                   stream->IsClosedForWrites();

//               if (stream->GetStreamId() == kInvalidStreamId) {
//                 GRPC_DCHECK(stream->IsStreamIdle());
//                 // TODO(akshitpatel) : [PH2][P5] : We will waste a stream id
//                 in
//                 // the rare scenario where the stream is aborted before it
//                 can
//                 // be written to. This is a possible area to optimize in
//                 future. absl::Status status = InitializeStream(*stream); if
//                 (!status.ok()) {
//                   GRPC_HTTP2_SERVER_DLOG
//                       << "Http2ServerTransport::MultiplexerLoop "
//                          "Failed to assign stream id and add to stream list
//                          for" " stream: "
//                       << stream.get() << " closing this stream.";
//                   BeginCloseStream(std::move(stream),
//                                    /*reset_stream_error_code=*/std::nullopt,
//                                    CancelledServerMetadataFromStatus(status));
//                   continue;
//                 }
//               }

//               if (GPR_LIKELY(!stream->IsClosedForWrites())) {
//                 absl::Status status = DequeueStreamFrames(
//                     std::move(stream),
//                     transport_write_context_.GetWriteCycle());
//                 if (GPR_UNLIKELY(!status.ok())) {
//                   GRPC_HTTP2_SERVER_DLOG
//                       << "Http2ServerTransport::MultiplexerLoop "
//                          "Failed to dequeue stream frames with status: "
//                       << status;
//                   return status;
//                 }
//               }
//             }

//             GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::MultiplexerLoop
//             "
//                                       "After draining all writable streams "
//                                    << write_cycle.DebugString();

//             return absl::OkStatus();
//           });
//         },
//         [this]() {
//           return Map(SerializeAndWrite(), [this](absl::Status status) {
//             if (GPR_UNLIKELY(!status.ok())) {
//               return status;
//             }
//             NotifyFramesWriteDone();
//             return absl::OkStatus();
//           });
//         },
//         [this]() -> LoopCtl<absl::Status> {
//           if (should_reset_ping_clock_) {
//             GRPC_HTTP2_SERVER_DLOG
//                 << "Http2ServerTransport::MultiplexerLoop ResetPingClock";
//             ping_manager_->ResetPingClock(/*is_client=*/kIsClient);
//             should_reset_ping_clock_ = false;
//           }
//           transport_write_context_.EndWriteCycle();
//           return Continue();
//         });
//   })));
// }

auto Http2ServerTransport::WriteFromQueue() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport WriteFromQueue Factory";
  return []() -> Poll<absl::Status> {
    // TODO(tjagtap) : [PH2][P2] : Implement this.
    // Read from the mpsc queue and write it to endpoint
    GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport WriteFromQueue Promise";
    return Pending{};
  };
}

auto Http2ServerTransport::WriteLoop() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport WriteLoop Factory";
  return AssertResultType<absl::Status>(Loop([this]() {
    return TrySeq(WriteFromQueue(), []() -> LoopCtl<absl::Status> {
      GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport WriteLoop Continue";
      return Continue();
    });
  }));
}

//////////////////////////////////////////////////////////////////////////////
// Spawn Helpers and Promise Helpers

//////////////////////////////////////////////////////////////////////////////
// Settings

// auto WaitForSettingsTimeoutOnDone();

// void MaybeSpawnWaitForSettingsTimeout();

// void EnforceLatestIncomingSettings();

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

  ActOnFlowControlActionSettings(
      action, settings_->mutable_local(),
      enable_preferred_rx_crypto_frame_advertisement_);

  if (action.AnyUpdateImmediately()) {
    // Prioritize sending flow control updates over reading data. If we
    // continue reading while urgent flow control updates are pending, we might
    // exhaust the flow control window. This prevents us from sending window
    // updates to the peer, causing the peer to block unnecessarily while
    // waiting for flow control tokens.
    reader_state_.SetPauseReadLoop();
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

// void Http2ServerTransport::AddToStreamList(RefCountedPtr<Stream> stream) {
//   bool should_wake_periodic_updates = false;
//   {
//     MutexLock lock(&transport_mutex_);
//     GRPC_DCHECK(stream != nullptr) << "stream is null";
//     GRPC_DCHECK_GT(stream->GetStreamId(), 0u) << "stream id is invalid";
//     GRPC_HTTP2_SERVER_DLOG
//         << "Http2ServerTransport::AddToStreamList for stream id: "
//         << stream->GetStreamId();
//     const uint32_t stream_id = stream->GetStreamId();
//     stream_list_.emplace(stream_id, std::move(stream));
//     // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
//     if (GetActiveStreamCountLocked() == 1) {
//       should_wake_periodic_updates = true;
//     }
//   }
//   // TODO(tjagtap) [PH2][P2][BDP] Remove this when the BDP code is done.
//   if (should_wake_periodic_updates) {
//     // Release the lock before you wake up another promise on the party.
//     WakeupPeriodicUpdatePromise();
//   }
// }

// absl::Status Http2ServerTransport::MaybeAddStreamToWritableStreamList(
//     RefCountedPtr<Stream> stream,
//     const StreamDataQueue<ClientMetadataHandle>::StreamWritabilityUpdate
//         result) {
//   if (result.became_writable) {
//     GRPC_HTTP2_SERVER_DLOG
//         << "Http2ServerTransport::MaybeAddStreamToWritableStreamList Stream "
//            "id: "
//         << stream->GetStreamId() << " became writable";
//     // TODO(akshitpatel) [Perf]: Might be worth exploring if this funciton
//     // should take a raw stream ptr and take a ref here.
//     absl::Status status =
//         writable_stream_list_.Enqueue(std::move(stream), result.priority);
//     if (!status.ok()) {
//       return HandleError(
//           std::nullopt,
//           Http2Status::Http2ConnectionError(
//               Http2ErrorCode::kRefusedStream,
//               "Failed to enqueue stream to writable stream list"));
//     }
//   }
//   return absl::OkStatus();
// }

// absl::StatusOr<uint32_t> Http2ServerTransport::NextStreamId() {
//   if (next_stream_id_ > GetMaxAllowedStreamId()) {
//     // TODO(tjagtap) : [PH2][P3] : Handle case if transport runs out of
//     stream
//     // ids
//     // RFC9113 : Stream identifiers cannot be reused. Long-lived connections
//     // can result in an endpoint exhausting the available range of stream
//     // identifiers. A client that is unable to establish a new stream
//     // identifier can establish a new connection for new streams. A server
//     // that is unable to establish a new stream identifier can send a GOAWAY
//     // frame so that the client is forced to open a new connection for new
//     // streams.
//     return absl::ResourceExhaustedError("No more stream ids available");
//   }
//   // TODO(akshitpatel) : [PH2][P3] : There is a channel arg to delay
//   // starting new streams instead of failing them. This needs to be
//   // implemented.
//   {
//     // TODO(tjagtap) : [PH2][P1] : For a server we will have to do
//     // this for incoming streams only. If a server receives more
//     // streams from a client than is allowed by the clients settings,
//     // whether or not we should fail is debatable.
//     MutexLock lock(&transport_mutex_);
//     if (GetActiveStreamCountLocked() >=
//         settings_->peer().max_concurrent_streams()) {
//       return absl::ResourceExhaustedError("Reached max concurrent streams");
//     }
//   }

//   // RFC9113 : Streams initiated by a client MUST use odd-numbered stream
//   // identifiers.
//   uint32_t new_stream_id = std::exchange(next_stream_id_, next_stream_id_ +
//   2); if (GPR_UNLIKELY(next_stream_id_ > GetMaxAllowedStreamId())) {
//     ReportDisconnection(
//         absl::ResourceExhaustedError("Transport Stream IDs exhausted"),
//         {},  // TODO(tjagtap) : [PH2][P2] : Report better disconnect info.
//         "no_more_stream_ids");
//   }
//   return new_stream_id;
// }

//////////////////////////////////////////////////////////////////////////////
// Stream Operations

// auto Http2ServerTransport::CallOutboundLoop(RefCountedPtr<Stream> stream) {
//   GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::CallOutboundLoop";
//   GRPC_DCHECK(stream != nullptr);

//   auto send_message = [this, stream](MessageHandle&& message) mutable {
//     return TrySeq(
//         stream->EnqueueMessage(std::move(message)),
//         [this, stream](const StreamWritabilityUpdate result) mutable {
//           GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::CallOutboundLoop "
//                                     "Enqueued Message";
//           return MaybeAddStreamToWritableStreamList(std::move(stream),
//           result);
//         });
//   };

//   auto send_initial_metadata =
//       [this, stream](ClientMetadataHandle&& metadata) mutable {
//         absl::StatusOr<StreamWritabilityUpdate> enqueue_result =
//             stream->EnqueueInitialMetadata(
//                 std::forward<ClientMetadataHandle>(metadata));
//         if (GPR_UNLIKELY(!enqueue_result.ok())) {
//           return enqueue_result.status();
//         }
//         GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport CallOutboundLoop "
//                                   "Enqueued Initial Metadata";
//         return MaybeAddStreamToWritableStreamList(std::move(stream),
//                                                   enqueue_result.value());
//       };

//   auto send_half_closed = [this, stream]() mutable {
//     absl::StatusOr<StreamWritabilityUpdate> enqueue_result =
//         stream->EnqueueHalfClosed();
//     if (GPR_UNLIKELY(!enqueue_result.ok())) {
//       return enqueue_result.status();
//     }
//     GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport CallOutboundLoop "
//                               "Enqueued Half Closed";
//     return MaybeAddStreamToWritableStreamList(std::move(stream),
//                                               enqueue_result.value());
//   };

//   return GRPC_LATENT_SEE_PROMISE(
//       "Ph2CallOutboundLoop",
//       TrySeq(
//           Map(stream->call.PullClientInitialMetadata(),
//               [send_initial_metadata = std::move(send_initial_metadata)](
//                   ValueOrFailure<ClientMetadataHandle> metadata) mutable {
//                 if (GPR_UNLIKELY(!metadata.ok())) {
//                   return absl::InternalError(
//                       "Failed to pull client initial metadata");
//                 }
//                 return std::move(send_initial_metadata)(
//                     TakeValue(std::move(metadata)));
//               }),
//           ForEach(MessagesFrom(stream->call), std::move(send_message)),
//           [send_half_closed = std::move(send_half_closed)]() mutable {
//             return std::move(send_half_closed)();
//           },
//           [stream]() mutable {
//             return Map(stream->call.WasCancelled(), [](bool cancelled) {
//               GRPC_HTTP2_SERVER_DLOG
//                   << "Http2ServerTransport::CallOutboundLoop End with "
//                      "cancelled="
//                   << cancelled;
//               return (cancelled) ? absl::CancelledError() : absl::OkStatus();
//             });
//           }));
// }

// absl::Status Http2ServerTransport::InitializeStream(Stream& stream) {
//   absl::StatusOr<uint32_t> next_stream_id = NextStreamId();
//   if (!next_stream_id.ok()) {
//     GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::InitializeStream "
//                               "Failed to get next stream id for stream: "
//                            << &stream;
//     return std::move(next_stream_id).status();
//   }
//   GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::InitializeStream "
//                             "Assigned stream id: "
//                          << next_stream_id.value() << " to stream: " <<
//                          &stream
//                          << ", allow_true_binary_metadata:"
//                          << settings_->peer().allow_true_binary_metadata();
//   stream.InitializeStream(next_stream_id.value(),
//                           settings_->peer().allow_true_binary_metadata(),
//                           settings_->acked().allow_true_binary_metadata());
//   return absl::OkStatus();
// }

// std::optional<RefCountedPtr<Stream>> Http2ServerTransport::MakeStream(
//     CallHandler call_handler) {
//   // https://datatracker.ietf.org/doc/html/rfc9113#name-stream-identifiers
//   RefCountedPtr<Stream> stream;
//   stream = MakeRefCounted<Stream>(call_handler, flow_control_,
//                                   /*is_client=*/kIsClient);
//   const bool on_done_added = SetOnDone(std::move(call_handler), stream);
//   if (!on_done_added) return std::nullopt;
//   return std::move(stream);
// }

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
// void Http2ServerTransport::BeginCloseStream(
//     RefCountedPtr<Stream> stream,
//     std::optional<uint32_t> reset_stream_error_code,
//     ServerMetadataHandle&& metadata, DebugLocation whence) {
//   if (stream == nullptr) {
//     GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::BeginCloseStream stream
//     "
//                               "is null reset_stream_error_code="
//                            << (reset_stream_error_code.has_value()
//                                    ? absl::StrCat(*reset_stream_error_code)
//                                    : "nullopt")
//                            << " metadata=" << metadata->DebugString();
//     return;
//   }

//   GRPC_HTTP2_SERVER_DLOG
//       << "Http2ServerTransport::BeginCloseStream for stream id: "
//       << stream->GetStreamId() << " error_code="
//       << (reset_stream_error_code.has_value()
//               ? absl::StrCat(*reset_stream_error_code)
//               : "nullopt")
//       << " ServerMetadata=" << metadata->DebugString()
//       << " location=" << whence.file() << ":" << whence.line();

//   bool close_reads = false;
//   bool close_writes = false;
//   if (metadata->get(GrpcCallWasCancelled())) {
//     if (!reset_stream_error_code) {
//       // Callers taking this path:
//       // 1. Reading a RST stream frame (will not send any frame out).
//       // 2. Closing a stream before initial metadata is sent.
//       close_reads = true;
//       close_writes = true;
//       GRPC_HTTP2_SERVER_DLOG
//           << "Http2ServerTransport::BeginCloseStream for stream id: "
//           << stream->GetStreamId() << " close_reads= " << close_reads
//           << " close_writes= " << close_writes;
//     } else {
//       // Callers taking this path:
//       // 1. Processing Error in transport (will send reset stream from here).
//       absl::StatusOr<StreamWritabilityUpdate> enqueue_result =
//           stream->EnqueueResetStream(reset_stream_error_code.value());
//       GRPC_HTTP2_SERVER_DLOG << "Enqueued ResetStream with error code="
//                              << reset_stream_error_code.value()
//                              << " status=" << enqueue_result.status();
//       if (enqueue_result.ok()) {
//         GRPC_UNUSED absl::Status status =
//             MaybeAddStreamToWritableStreamList(stream,
//             enqueue_result.value());
//       }
//       close_reads = true;
//       GRPC_HTTP2_SERVER_DLOG
//           << "Http2ServerTransport::BeginCloseStream for stream id: "
//           << stream->GetStreamId() << " close_reads= " << close_reads
//           << " close_writes= " << close_writes;
//     }
//   } else {
//     // Callers taking this path:
//     // 1. Reading Trailing Metadata (MAY send half close from OnDone).
//     // If a half close frame has already been sent, we should close the
//     stream
//     // for reads and writes.
//     if (stream->IsHalfClosedLocal() || stream->IsStreamClosed()) {
//       close_reads = true;
//       close_writes = true;
//       GRPC_HTTP2_SERVER_DLOG
//           << "Http2ServerTransport::BeginCloseStream for stream id: "
//           << stream->GetStreamId() << " close_reads= " << close_reads
//           << " close_writes= " << close_writes;
//     }
//   }

//   if (close_reads || close_writes) {
//     CloseStream(*stream, CloseStreamArgs{close_reads, close_writes}, whence);
//   }

//   // If the call was cancelled, the stream MUST be closed for reads.
//   GRPC_DCHECK(metadata->get(GrpcCallWasCancelled()) ? close_reads : true);

//   // This maybe called multiple times while closing a stream. In CallV3, the
//   // flow for pushing server trailing metadata is idempotent. However, there
//   is
//   // a subtle difference. When we push server trailing metadata with a
//   cancelled
//   // status PushServerTrailingMetadata is spawned inline on the Call party
//   // whereas for the non-cancelled status, PushServerTrailingMetadata is
//   // spawned in the server_to_client spawn serializer. Because of this, in
//   // case when the server pushes trailing metadata (non-cancelled) followed
//   by a
//   // RST stream with cancelled status, it is possible that the cancelled
//   // trailing metadata (for RST stream) is processed before. This would
//   result
//   // in losing the actual status/message pushed by the server.
//   // To address this, we push the server trailing metadata to the stream only
//   // if it is not pushed already.
//   stream->MaybePushServerTrailingMetadata(std::move(metadata));
// }

// This function MUST be idempotent. This function MUST be called from the
// transport party.
// void Http2ServerTransport::CloseStream(Stream& stream, CloseStreamArgs args,
//                                        DebugLocation whence) {
//   std::optional<Http2Status> close_transport_error;

//   {
//     // TODO(akshitpatel) : [PH2][P3] : Measure the impact of holding mutex
//     // throughout this function.
//     MutexLock lock(&transport_mutex_);
//     GRPC_HTTP2_SERVER_DLOG
//         << "Http2ServerTransport::CloseStream for stream id: "
//         << stream.GetStreamId() << " close_reads=" << args.close_reads
//         << " close_writes=" << args.close_writes
//         << " incoming_headers_=" << incoming_headers_.DebugString()
//         << " location=" << whence.file() << ":" << whence.line();

//     if (args.close_writes) {
//       stream.SetWriteClosed();
//     }

//     if (args.close_reads) {
//       GRPC_HTTP2_SERVER_DLOG
//           << "Http2ServerTransport::CloseStream for stream id: "
//           << stream.GetStreamId() << " closing stream for reads.";
//       // If the stream is closed while reading HEADER/CONTINUATION frames, we
//       // should still parse the enqueued buffer to maintain HPACK state
//       between
//       // peers.
//       if (incoming_headers_.IsWaitingForContinuationFrame()) {
//         Http2Status result = http2::ParseAndDiscardHeaders(
//             parser_, SliceBuffer(),
//             HeaderAssembler::ParseHeaderArgs{
//                 /*is_initial_metadata=*/!incoming_headers_.HeaderHasEndStream(),
//                 /*is_end_headers=*/false,
//                 /*is_client=*/kIsClient,
//                 /*max_header_list_size_soft_limit=*/
//                 incoming_headers_.soft_limit(),
//                 /*max_header_list_size_hard_limit=*/
//                 settings_->acked().max_header_list_size(),
//                 /*stream_id=*/incoming_headers_.GetStreamId(),
//             },
//             &stream, /*original_status=*/Http2Status::Ok());
//         if (result.GetType() ==
//         Http2Status::Http2ErrorType::kConnectionError) {
//           GRPC_HTTP2_SERVER_DLOG
//               << "Http2ServerTransport::CloseStream for stream id: "
//               << stream.GetStreamId() << " failed to partially process
//               header: "
//               << result.DebugString();
//           close_transport_error.emplace(std::move(result));
//         }
//       }

//       stream_list_.erase(stream.GetStreamId());
//       if (!close_transport_error.has_value() && CanCloseTransportLocked()) {
//         // TODO(akshitpatel) : [PH2][P3] : Is kInternalError the right error
//         // code to use here? IMO it should be kNoError.
//         close_transport_error.emplace(Http2Status::Http2ConnectionError(
//             Http2ErrorCode::kInternalError,
//             std::string(RFC9113::kLastStreamClosed)));
//       }
//     }
//   }

//   if (close_transport_error.has_value()) {
//     GRPC_UNUSED absl::Status status = HandleError(
//         /*stream_id=*/std::nullopt, std::move(*close_transport_error));
//   }
// }

//////////////////////////////////////////////////////////////////////////////
// Ping Keepalive and Goaway

// void Http2ServerTransport::MaybeSpawnPingTimeout(
//     std::optional<uint64_t> opaque_data) {
//   if (opaque_data.has_value()) {
//     SpawnGuardedTransportParty(
//         "PingTimeout", [self = RefAsSubclass<Http2ServerTransport>(),
//                         opaque_data = *opaque_data]() {
//           return self->ping_manager_->TimeoutPromise(opaque_data);
//         });
//   }
// }
// void Http2ServerTransport::MaybeSpawnDelayedPing(
//     std::optional<Duration> delayed_ping_wait) {
//   if (delayed_ping_wait.has_value()) {
//     SpawnGuardedTransportParty(
//         "DelayedPing", [self = RefAsSubclass<Http2ServerTransport>(),
//                         wait = *delayed_ping_wait]() {
//           GRPC_HTTP2_PING_LOG << "Scheduling delayed ping after wait=" <<
//           wait; return self->ping_manager_->DelayedPingPromise(wait);
//         });
//   }
// }

// absl::Status Http2ServerTransport::AckPing(uint64_t opaque_data) {
//   // It is possible that the PingRatePolicy may decide to not send a ping
//   // request (in cases like the number of inflight pings is too high).
//   // When this happens, it becomes important to ensure that if a ping ack
//   // is received and there is an "important" outstanding ping request, we
//   // should retry to send it out now.
//   if (ping_manager_->AckPing(opaque_data)) {
//     if (ping_manager_->ImportantPingRequested()) {
//       return TriggerWriteCycle();
//     }
//   } else {
//     GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::AckPing Unknown ping "
//                               "response received for ping id="
//                            << opaque_data;
//   }

//   return absl::OkStatus();
// }

// void Http2ServerTransport::MaybeSpawnKeepaliveLoop() {
//   if (keepalive_manager_->IsKeepAliveLoopNeeded()) {
//     SpawnGuardedTransportParty(
//         "KeepaliveLoop", [self = RefAsSubclass<Http2ServerTransport>()]() {
//           return self->keepalive_manager_->KeepaliveLoop();
//         });
//   }
// }

//////////////////////////////////////////////////////////////////////////////
// Error Path and Close Path

// void Http2ServerTransport::MaybeSpawnCloseTransport(Http2Status http2_status,
//                                                     DebugLocation whence) {
//   GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::MaybeSpawnCloseTransport "
//                             "status="
//                          << http2_status << " location=" << whence.file() <<
//                          ":"
//                          << whence.line();

//   // Free up the stream_list at this point. This would still allow the frames
//   // in the MPSC to be drained and block any additional frames from being
//   // enqueued. Additionally this also prevents additional frames with
//   non-zero
//   // stream_ids from being processed by the read loop.
//   ReleasableMutexLock lock(&transport_mutex_);
//   if (is_transport_closed_) {
//     lock.Release();
//     return;
//   }
//   GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::MaybeSpawnCloseTransport "
//                             "Initiating transport close";
//   is_transport_closed_ = true;
//   absl::flat_hash_map<uint32_t, RefCountedPtr<Stream>> stream_list =
//       std::move(stream_list_);
//   stream_list_.clear();
//   ReportDisconnectionLocked(
//       http2_status.GetAbslConnectionError(), {},
//       absl::StrCat("Transport closed: ",
//       http2_status.DebugString()).c_str());
//   lock.Release();

//   SpawnInfallibleTransportParty(
//       "CloseTransport", [self = RefAsSubclass<Http2ServerTransport>(),
//                          stream_list = std::move(stream_list),
//                          http2_status = std::move(http2_status)]() mutable {
//         self->security_frame_handler_->OnTransportClosed();
//         GRPC_HTTP2_SERVER_DLOG
//             << "Http2ServerTransport::MaybeSpawnCloseTransport "
//                "Cleaning up call stacks";
//         // Clean up the call stacks for all active streams.
//         for (const auto& pair : stream_list) {
//           // There is no merit in transitioning the stream to
//           // closed state here as the subsequent lookups would
//           // fail. Also, as this is running on the transport
//           // party, there would not be concurrent access to the stream.
//           RefCountedPtr<Stream> stream = pair.second;
//           self->BeginCloseStream(std::move(stream),
//                                  Http2ErrorCodeToFrameErrorCode(
//                                      http2_status.GetConnectionErrorCode()),
//                                  CancelledServerMetadataFromStatus(
//                                      http2_status.GetAbslConnectionError()));
//         }

//         // RFC9113 : A GOAWAY frame might not immediately precede closing of
//         // the connection; a receiver of a GOAWAY that has no more use for
//         the
//         // connection SHOULD still send a GOAWAY frame before terminating the
//         // connection.
//         return Map(
//             // TODO(akshitpatel) : [PH2][P4] : This is creating a copy of
//             // the debug data. Verify if this is causing a performance
//             // issue.
//             Race(AssertResultType<absl::Status>(
//                      self->goaway_manager_.RequestGoaway(
//                          http2_status.GetConnectionErrorCode(),
//                          /*debug_data=*/
//                          Slice::FromCopiedString(
//                              http2_status.GetAbslConnectionError().message()),
//                          kLastIncomingStreamIdClient, /*immediate=*/true)),
//                  // Failsafe to close the transport if goaway is not
//                  // sent within kGoawaySendTimeoutSeconds seconds.
//                  Sleep(Duration::Seconds(kGoawaySendTimeoutSeconds))),
//             [self](auto) mutable {
//               self->CloseTransport();
//               return Empty{};
//             });
//         ;
//       });
// }

// bool Http2ServerTransport::CanCloseTransportLocked() const {
//   // If there are no more streams and next stream id is greater than the
//   // max allowed stream id, then no more streams can be created and it is
//   // safe to close the transport.
//   GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::CanCloseTransportLocked "
//                             "GetActiveStreamCountLocked="
//                          << GetActiveStreamCountLocked()
//                          << " PeekNextStreamId=" << PeekNextStreamId()
//                          << " GetMaxAllowedStreamId="
//                          << GetMaxAllowedStreamId();
//   return GetActiveStreamCountLocked() == 0 &&
//          PeekNextStreamId() > GetMaxAllowedStreamId();
// }

// void Http2ServerTransport::CloseTransport() {
//   GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::CloseTransport";

//   transport_closed_latch_.Set();
//   settings_->HandleTransportShutdown(event_engine_.get());

//   // This is the only place where the general_party_ is reset.
//   general_party_.reset();
// }

//////////////////////////////////////////////////////////////////////////////
// Misc Transport Stuff

void Http2ServerTransport::ReportDisconnection(
    const absl::Status& status, StateWatcher::DisconnectInfo disconnect_info,
    const char* reason) {
  MutexLock lock(&transport_mutex_);
  ReportDisconnectionLocked(status, disconnect_info, reason);
}

void Http2ServerTransport::ReportDisconnectionLocked(
    const absl::Status& status, StateWatcher::DisconnectInfo disconnect_info,
    const char* reason) {
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport::ReportDisconnectionLocked status="
      << status.ToString() << "; reason=" << reason;
  state_tracker_.SetState(GRPC_CHANNEL_TRANSIENT_FAILURE, status, reason);
  NotifyStateWatcherOnDisconnectLocked(status, disconnect_info);
}

// bool Http2ServerTransport::SetOnDone(CallHandler call_handler,
//                                      RefCountedPtr<Stream> stream) {
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
  incoming_headers_.set_soft_limit(args.max_header_list_size_soft_limit);
  keepalive_permit_without_calls_ = args.keepalive_permit_without_calls;
  enable_preferred_rx_crypto_frame_advertisement_ =
      args.enable_preferred_rx_crypto_frame_advertisement;
  test_only_ack_pings_ = args.test_only_ack_pings;

  if (args.initial_sequence_number > 0) {
    next_stream_id_ = args.initial_sequence_number;
  }

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
  GRPC_HTTP2_SERVER_DLOG << "Ping timeout at time: " << Timestamp::Now();

  // TODO(akshitpatel) : [PH2][P2] : The error code here has been chosen
  // based on CHTTP2's usage of GRPC_STATUS_UNAVAILABLE (which corresponds
  // to kRefusedStream). However looking at RFC9113, definition of
  // kRefusedStream doesn't seem to fit this case. We should revisit this
  // and update the error code.
  return Immediate(transport_->HandleError(
      std::nullopt,
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
  GRPC_HTTP2_SERVER_DLOG << "Keepalive timeout triggered";
  // TODO(akshitpatel) : [PH2][P2] : The error code here has been chosen
  // based on CHTTP2's usage of GRPC_STATUS_UNAVAILABLE (which corresponds
  // to kRefusedStream). However looking at RFC9113, definition of
  // kRefusedStream doesn't seem to fit this case. We should revisit this
  // and update the error code.
  return Immediate(transport_->HandleError(
      std::nullopt,
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
  // TODO(akshitpatel) : [PH2][P1] : This function is not needed for a client.
  // Implement this for the server.
  return 0;
}

//////////////////////////////////////////////////////////////////////////////
// Constructor, Destructor etc.

Http2ServerTransport::Http2ServerTransport(
    PromiseEndpoint endpoint, GRPC_UNUSED const ChannelArgs& channel_args,
    std::shared_ptr<EventEngine> event_engine)
    : channelz::DataSource(http2::CreateChannelzSocketNode(
          endpoint.GetEventEngineEndpoint(), channel_args)),
      outgoing_frames_(10),
      endpoint_(std::move(endpoint)),
      settings_(MakeRefCounted<SettingsPromiseManager>(nullptr)),
      incoming_headers_(IncomingMetadataTracker::GetPeerString(endpoint_)),
      ping_manager_(std::nullopt),
      keepalive_manager_(std::nullopt),
      goaway_manager_(GoawayInterfaceImpl::Make(this)),
      security_frame_handler_(MakeRefCounted<SecurityFrameHandler>()),
      memory_owner_(channel_args.GetObject<ResourceQuota>()
                        ->memory_quota()
                        ->CreateMemoryOwner()),
      flow_control_(
          "PH2_Server",
          channel_args.GetBool(GRPC_ARG_HTTP2_BDP_PROBE).value_or(true),
          &memory_owner_),
      ztrace_collector_(std::make_shared<PromiseHttp2ZTraceCollector>()) {
  // TODO(tjagtap) : [PH2][P2] : Save and apply channel_args.
  // TODO(tjagtap) : [PH2][P2] : Initialize settings_ to appropriate values.

  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Constructor Begin";
  SourceConstructed();

  // Initialize the general party and write party.
  auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
  general_party_arena->SetContext<EventEngine>(event_engine.get());
  general_party_ = Party::Make(std::move(general_party_arena));

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
  // TODO(tjagtap) : [PH2][P2] : Implement the needed cleanup
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Destructor Begin";
  general_party_.reset();
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Destructor End";
}

//////////////////////////////////////////////////////////////////////////////
// Transport Functions

void Http2ServerTransport::SetCallDestination(
    RefCountedPtr<UnstartedCallDestination> call_destination) {
  // TODO(tjagtap) : [PH2][P2] : Implement this function.
  GRPC_CHECK(call_destination_ == nullptr);
  GRPC_CHECK(call_destination != nullptr);
  call_destination_ = call_destination;
  // got_acceptor_.Set(); // Copied from CG. Understand and fix.
}

void Http2ServerTransport::PerformOp(GRPC_UNUSED grpc_transport_op*) {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport PerformOp Begin";
  // TODO(tjagtap) : [PH2][P2] : Implement this function.
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport PerformOp End";
}

void Http2ServerTransport::Orphan() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Orphan Begin";
  SourceDestructing();
  // TODO(tjagtap) : [PH2][P2] : Implement the needed cleanup
  general_party_.reset();
  Unref();
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Orphan End";
}

void Http2ServerTransport::SpawnTransportLoops() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::SpawnTransportLoops Begin";
  // MaybeSpawnKeepaliveLoop();

  // SpawnGuardedTransportParty(
  //     "FlowControlPeriodicUpdateLoop",
  //     UntilTransportClosed(FlowControlPeriodicUpdateLoop()));

  if (!TriggerWriteCycleOrHandleError()) {
    return;
  }
  // For Client, write happens before read. So MultiplexerLoop is spawned first.
  // ReadLoop is spawned after the first write.
  // For Server, read happens before write. So ReadLoop is spawned first.
  // MultiplexerLoop is spawned after the first read.
  SpawnGuardedTransportParty("ReadLoop", UntilTransportClosed(ReadLoop()));

  // TODO(tjagtap) : [PH2][P0] : Spawn MultiplexerLoop after 1st read completes.
  // TODO(tjagtap) : [PH2][P0] : Remove this when MultiplexerLoop is implemented
  SpawnGuardedTransportParty("WriteLoop", WriteLoop());

  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport::SpawnTransportLoops End";
}

}  // namespace http2
}  // namespace grpc_core
