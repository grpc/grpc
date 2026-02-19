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

#define GRPC_HTTP2_SERVER_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

using grpc_event_engine::experimental::EventEngine;

// Experimental : This is just the initial skeleton of class
// and it is functions. The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P3] : Delete this comment when http2
// rollout begins

// TODO(akshitpatel) : [PH2][P2] : Choose appropriate size later.
// TODO(tjagtap) : [PH2][P2] : Consider moving to common code.
constexpr int kMpscSize = 10;

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
  // TODO(tjagtap) : [PH2][P2] : Implement the needed cleanup
  general_party_.reset();
  Unref();
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Orphan End";
}

void Http2ServerTransport::AbortWithError() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport AbortWithError Begin";
  // TODO(tjagtap) : [PH2][P2] : Implement this function.
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport AbortWithError End";
}

//////////////////////////////////////////////////////////////////////////////
// Channelz and ZTrace

//////////////////////////////////////////////////////////////////////////////
// Watchers

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
// Transport Read Path

// Http2Status Http2ServerTransport::ProcessIncomingFrame(Http2DataFrame&&
// frame) {
//   // https://www.rfc-editor.org/rfc/rfc9113.html#name-data
//   GRPC_HTTP2_SERVER_DLOG
//       << "Http2ServerTransport ProcessHttp2DataFrame { stream_id="
//       << frame.stream_id << ", end_stream:" << frame.end_stream
//       << ", payload length=" << frame.payload.Length() << "}";

//   // TODO(akshitpatel) : [PH2][P3] : Investigate if we should do this even if
//   // the function returns a non-ok status?
//   ping_manager_->ReceivedDataFrame();

//   RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);

//   ValueOrHttp2Status<chttp2::FlowControlAction> flow_control_action =
//       ProcessIncomingDataFrameFlowControl(current_frame_header_,
//       flow_control_,
//                                           stream);
//   if (!flow_control_action.IsOk()) {
//     return ValueOrHttp2Status<chttp2::FlowControlAction>::TakeStatus(
//         std::move(flow_control_action));
//   }
//   ActOnFlowControlAction(flow_control_action.value(), stream);

//   if (stream == nullptr) {
//     // TODO(tjagtap) : [PH2][P2] : Implement the correct behaviour later.
//     // RFC9113 : If a DATA frame is received whose stream is not in the
//     "open"
//     // or "half-closed (local)" state, the recipient MUST respond with a
//     stream
//     // error (Section 5.4.2) of type STREAM_CLOSED.
//     GRPC_HTTP2_SERVER_DLOG
//         << "Http2ServerTransport ProcessHttp2DataFrame { stream_id="
//         << frame.stream_id << "} Lookup Failed";
//     return Http2Status::Ok();
//   }

//   // TODO(akshitpatel) : [PH2][P3] : We should add a check to reset stream if
//   // the stream state is kIdle as well.

//   Http2Status stream_status = stream->CanStreamReceiveDataFrames();
//   if (!stream_status.IsOk()) {
//     return stream_status;
//   }

//   // Add frame to assembler
//   GRPC_HTTP2_SERVER_DLOG
//       << "Http2ServerTransport ProcessHttp2DataFrame AppendNewDataFrame";
//   GrpcMessageAssembler& assembler = stream->assembler;
//   Http2Status status =
//       assembler.AppendNewDataFrame(frame.payload, frame.end_stream);
//   if (!status.IsOk()) {
//     GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessHttp2DataFrame "
//                               "AppendNewDataFrame Failed";
//     return status;
//   }

//   // Pass the messages up the stack if it is ready.
//   while (true) {
//     GRPC_HTTP2_SERVER_DLOG
//         << "Http2ServerTransport ProcessHttp2DataFrame ExtractMessage";
//     ValueOrHttp2Status<MessageHandle> result = assembler.ExtractMessage();
//     if (!result.IsOk()) {
//       GRPC_HTTP2_SERVER_DLOG
//           << "Http2ServerTransport ProcessHttp2DataFrame ExtractMessage
//           Failed";
//       return
//       ValueOrHttp2Status<MessageHandle>::TakeStatus(std::move(result));
//     }
//     MessageHandle message = TakeValue(std::move(result));
//     if (message != nullptr) {
//       GRPC_HTTP2_SERVER_DLOG
//           << "Http2ServerTransport ProcessHttp2DataFrame SpawnPushMessage "
//           << message->DebugString();
//       stream->call.SpawnPushMessage(std::move(message));
//       continue;
//     }
//     GRPC_HTTP2_SERVER_DLOG
//         << "Http2ServerTransport ProcessHttp2DataFrame While Break";
//     break;
//   }

//   // TODO(tjagtap) : [PH2][P2] : List of Tests:
//   // 1. Data frame with unknown stream ID
//   // 2. Data frame with only half a message and then end stream
//   // 3. One data frame with a full message
//   // 4. Three data frames with one full message
//   // 5. One data frame with three full messages. All messages should be
//   pushed.
//   // Will need to mock the call_handler object and test this along with the
//   // Header reading code. Because we need a stream in place for the lookup to
//   // work.
//   return Http2Status::Ok();
// }

// Http2Status Http2ServerTransport::ProcessIncomingFrame(
//     Http2HeaderFrame&& frame) {
//   // https://www.rfc-editor.org/rfc/rfc9113.html#name-headers
//   GRPC_HTTP2_SERVER_DLOG
//       << "Http2ServerTransport ProcessHttp2HeaderFrame Promise { stream_id="
//       << frame.stream_id << ", end_headers=" << frame.end_headers
//       << ", end_stream=" << frame.end_stream << " }";
//   // State update MUST happen before processing the frame.
//   incoming_headers_.OnHeaderReceived(frame);

//   ping_manager_->ReceivedDataFrame();

//   RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
//   if (stream == nullptr) {
//     // TODO(tjagtap) : [PH2][P3] : Implement this.
//     // RFC9113 : The identifier of a newly established stream MUST be
//     // numerically greater than all streams that the initiating endpoint has
//     // opened or reserved. This governs streams that are opened using a
//     HEADERS
//     // frame and streams that are reserved using PUSH_PROMISE. An endpoint
//     that
//     // receives an unexpected stream identifier MUST respond with a
//     connection
//     // error (Section 5.4.1) of type PROTOCOL_ERROR.
//     GRPC_HTTP2_SERVER_DLOG
//         << "Http2ServerTransport ProcessHttp2HeaderFrame Promise {
//         stream_id="
//         << frame.stream_id << "} Lookup Failed";
//     return ParseAndDiscardHeaders(std::move(frame.payload),
//     frame.end_headers,
//                                   /*stream=*/nullptr, Http2Status::Ok());
//   }

//   if (stream->IsStreamHalfClosedRemote()) {
//     return ParseAndDiscardHeaders(
//         std::move(frame.payload), frame.end_headers, stream,
//         Http2Status::Http2StreamError(
//             Http2ErrorCode::kStreamClosed,
//             std::string(RFC9113::kHalfClosedRemoteState)));
//   }

//   if (incoming_headers_.ClientReceivedDuplicateMetadata(
//           stream->did_receive_initial_metadata,
//           stream->did_receive_trailing_metadata)) {
//     return ParseAndDiscardHeaders(
//         std::move(frame.payload), frame.end_headers, stream,
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
//                                   stream, std::move(append_result));
//   }

//   Http2Status status = ProcessMetadata(stream);
//   if (!status.IsOk()) {
//     // Frame payload has been moved to the HeaderAssembler. So calling
//     // ParseAndDiscardHeaders with an empty buffer.
//     return ParseAndDiscardHeaders(SliceBuffer(), frame.end_headers, stream,
//                                   std::move(status));
//   }

//   // Frame payload has either been processed or moved to the HeaderAssembler.
//   return Http2Status::Ok();
// }

// Http2Status Http2ServerTransport::ProcessIncomingFrame(
//     Http2RstStreamFrame&& frame) {
//   // https://www.rfc-editor.org/rfc/rfc9113.html#name-rst_stream
//   GRPC_HTTP2_SERVER_DLOG
//       << "Http2ServerTransport ProcessHttp2RstStreamFrame { stream_id="
//       << frame.stream_id << ", error_code=" << frame.error_code << " }";

//   Http2ErrorCode error_code =
//   FrameErrorCodeToHttp2ErrorCode(frame.error_code); absl::Status status =
//   absl::Status(ErrorCodeToAbslStatusCode(error_code),
//                                      "Reset stream frame received.");
//   RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
//   if (stream != nullptr) {
//     stream->MarkHalfClosedRemote();
//     BeginCloseStream(stream, /*reset_stream_error_code=*/std::nullopt,
//                      CancelledServerMetadataFromStatus(status));
//   }

//   // In case of stream error, we do not want the Read Loop to be broken.
//   Hence
//   // returning an ok status.
//   return Http2Status::Ok();
// }

// Http2Status Http2ServerTransport::ProcessIncomingFrame(
//     Http2SettingsFrame&& frame) {
//   // https://www.rfc-editor.org/rfc/rfc9113.html#name-settings

//   GRPC_HTTP2_SERVER_DLOG
//       << "Http2ServerTransport ProcessHttp2SettingsFrame { ack=" << frame.ack
//       << ", settings length=" << frame.settings.size() << "}";

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
//       // TODO(tjagtap) [PH2][P4] : The RFC does not say anything about what
//       // should happen if we receive an unsolicited SETTINGS ACK. Decide if
//       we
//       // want to respond with any error or just proceed.
//       LOG(ERROR) << "Settings ack received without sending settings";
//     }
//   }

//   return Http2Status::Ok();
// }

// Http2Status Http2ServerTransport::ProcessIncomingFrame(Http2PingFrame&&
// frame) {
//   // https://www.rfc-editor.org/rfc/rfc9113.html#name-ping
//   GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessHttp2PingFrame {
//   ack="
//                          << frame.ack << ", opaque=" << frame.opaque << " }";
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
//           << "Http2ServerTransport ProcessHttp2PingFrame "
//              "test_only_ack_pings_ is false. Ignoring the ping "
//              "request.";
//     }
//   }
//   return Http2Status::Ok();
// }

// Http2Status Http2ServerTransport::ProcessIncomingFrame(
//     Http2GoawayFrame&& frame) {
//   // https://www.rfc-editor.org/rfc/rfc9113.html#name-goaway
//   GRPC_HTTP2_SERVER_DLOG
//       << "Http2ServerTransport ProcessHttp2GoawayFrame Promise { "
//          "last_stream_id="
//       << frame.last_stream_id << ", error_code=" << frame.error_code << "}";
//   LOG_IF(ERROR,
//          frame.error_code != static_cast<uint32_t>(Http2ErrorCode::kNoError))
//       << "Received GOAWAY frame with error code: " << frame.error_code;

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
//       GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessHttp2GoawayFrame
//       "
//                                 "stream_list_ is empty";
//     }
//   }

//   StateWatcher::DisconnectInfo disconnect_info;
//   disconnect_info.reason = Transport::StateWatcher::kGoaway;
//   disconnect_info.http2_error_code =
//       static_cast<Http2ErrorCode>(frame.error_code);

//   // Throttle keepalive time if the server sends a GOAWAY with error code
//   // ENHANCE_YOUR_CALM and debug data equal to "too_many_pings". This will
//   // apply to any new transport created on by any subchannel of this channel.
//   if (GPR_UNLIKELY(frame.error_code == static_cast<uint32_t>(
//                                            Http2ErrorCode::kEnhanceYourCalm)
//                                            &&
//                    frame.debug_data == "too_many_pings")) {
//     LOG(ERROR) << ": Received a GOAWAY with error code ENHANCE_YOUR_CALM and
//     "
//                   "debug data equal to \"too_many_pings\". Current keepalive
//                   " "time (before throttling): "
//                << keepalive_time_.ToString();
//     constexpr int max_keepalive_time_millis =
//         INT_MAX / KEEPALIVE_TIME_BACKOFF_MULTIPLIER;
//     uint64_t throttled_keepalive_time =
//         keepalive_time_.millis() > max_keepalive_time_millis
//             ? INT_MAX
//             : keepalive_time_.millis() * KEEPALIVE_TIME_BACKOFF_MULTIPLIER;
//     if (!IsSubchannelConnectionScalingEnabled()) {
//       status.SetPayload(kKeepaliveThrottlingKey,
//                         absl::Cord(std::to_string(throttled_keepalive_time)));
//     }
//     disconnect_info.keepalive_time =
//         Duration::Milliseconds(throttled_keepalive_time);
//   }

//   if (close_transport) {
//     // TODO(akshitpatel) : [PH2][P3] : Ideally the error here should be
//     // kNoError. However, Http2Status does not support kNoError. We should
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
//   return Http2Status::Ok();
// }

// Http2Status Http2ServerTransport::ProcessIncomingFrame(
//     Http2WindowUpdateFrame&& frame) {
//   // https://www.rfc-editor.org/rfc/rfc9113.html#name-window_update
//   GRPC_HTTP2_SERVER_DLOG
//       << "Http2ServerTransport ProcessHttp2WindowUpdateFrame Promise { "
//          " stream_id="
//       << frame.stream_id << ", increment=" << frame.increment << "}";

//   RefCountedPtr<Stream> stream = nullptr;
//   if (frame.stream_id != 0) {
//     stream = LookupStream(frame.stream_id);
//   }
//   if (stream != nullptr) {
//     StreamWritabilityUpdate update =
//         stream->ReceivedFlowControlWindowUpdate(frame.increment);
//     if (update.became_writable) {
//       absl::Status status = writable_stream_list_.EnqueueWrapper(
//           stream, update.priority, AreTransportFlowControlTokensAvailable());
//       if (!status.ok()) {
//         return ToHttpOkOrConnError(status);
//       }
//     }
//   }

//   const bool should_trigger_write =
//       ProcessIncomingWindowUpdateFrameFlowControl(frame, flow_control_,
//       stream);
//   if (should_trigger_write) {
//     return ToHttpOkOrConnError(TriggerWriteCycle());
//   }
//   return Http2Status::Ok();
// }

// Http2Status Http2ServerTransport::ProcessIncomingFrame(
//     Http2ContinuationFrame&& frame) {
//   // https://www.rfc-editor.org/rfc/rfc9113.html#name-continuation
//   GRPC_HTTP2_SERVER_DLOG
//       << "Http2ServerTransport ProcessHttp2ContinuationFrame Promise { "
//          "stream_id="
//       << frame.stream_id << ", end_headers=" << frame.end_headers << " }";

//   // State update MUST happen before processing the frame.
//   incoming_headers_.OnContinuationReceived(frame);

//   RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
//   if (stream == nullptr) {
//     // TODO(tjagtap) : [PH2][P3] : Implement this.
//     // RFC9113 : The identifier of a newly established stream MUST be
//     // numerically greater than all streams that the initiating endpoint has
//     // opened or reserved. This governs streams that are opened using a
//     HEADERS
//     // frame and streams that are reserved using PUSH_PROMISE. An endpoint
//     that
//     // receives an unexpected stream identifier MUST respond with a
//     connection
//     // error (Section 5.4.1) of type PROTOCOL_ERROR.
//     return ParseAndDiscardHeaders(std::move(frame.payload),
//     frame.end_headers,
//                                   nullptr, Http2Status::Ok());
//   }

//   if (stream->IsStreamHalfClosedRemote()) {
//     return ParseAndDiscardHeaders(
//         std::move(frame.payload), frame.end_headers, stream,
//         Http2Status::Http2StreamError(
//             Http2ErrorCode::kStreamClosed,
//             std::string(RFC9113::kHalfClosedRemoteState)));
//   }

//   Http2Status append_result =
//       stream->header_assembler.AppendContinuationFrame(frame);
//   if (!append_result.IsOk()) {
//     // Frame payload is not consumed if AppendContinuationFrame returns a
//     // non-OK status. We need to process it to keep our in consistent state.
//     return ParseAndDiscardHeaders(std::move(frame.payload),
//     frame.end_headers,
//                                   stream, std::move(append_result));
//   }

//   Http2Status status = ProcessMetadata(stream);
//   if (!status.IsOk()) {
//     // Frame payload is consumed by HeaderAssembler. So passing an empty
//     // SliceBuffer to ParseAndDiscardHeaders.
//     return ParseAndDiscardHeaders(SliceBuffer(), frame.end_headers, stream,
//                                   std::move(status));
//   }

//   // Frame payload has either been processed or moved to the HeaderAssembler.
//   return Http2Status::Ok();
// }

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    Http2SecurityFrame&& frame) {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessHttp2SecurityFrame";
  if (settings_->IsSecurityFrameExpected()) {
    security_frame_handler_->ProcessPayload(std::move(frame.payload));
  }
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    GRPC_UNUSED Http2UnknownFrame&& frame) {
  // RFC9113: Implementations MUST ignore and discard frames of
  // unknown types.
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessHttp2UnknownFrame ";
  return Http2Status::Ok();
}

Http2Status Http2ServerTransport::ProcessIncomingFrame(
    GRPC_UNUSED Http2EmptyFrame&& frame) {
  LOG(DFATAL) << "ParseFramePayload should never return a Http2EmptyFrame";
  return Http2Status::Ok();
}

// Http2Status Http2ServerTransport::ProcessMetadata(
//     RefCountedPtr<Stream> stream) {
//   HeaderAssembler& assembler = stream->header_assembler;
//   CallHandler call = stream->call;

//   GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessMetadata";
//   if (assembler.IsReady()) {
//     ValueOrHttp2Status<ServerMetadataHandle> read_result =
//         assembler.ReadMetadata(parser_,
//         !incoming_headers_.HeaderHasEndStream(),
//                                /*is_client=*/true,
//                                /*max_header_list_size_soft_limit=*/
//                                incoming_headers_.soft_limit(),
//                                /*max_header_list_size_hard_limit=*/
//                                settings_->acked().max_header_list_size());
//     if (read_result.IsOk()) {
//       ServerMetadataHandle metadata = TakeValue(std::move(read_result));
//       if (incoming_headers_.HeaderHasEndStream()) {
//         // TODO(tjagtap) : [PH2][P1] : Is this the right way to differentiate
//         // between initial and trailing metadata?
//         stream->MarkHalfClosedRemote();
//         stream->did_receive_trailing_metadata = true;
//         BeginCloseStream(stream, /*reset_stream_error_code=*/std::nullopt,
//                          std::move(metadata));
//       } else {
//         GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessMetadata "
//                                   "SpawnPushServerInitialMetadata";
//         metadata->Set(PeerString(), incoming_headers_.peer_string());
//         stream->did_receive_initial_metadata = true;
//         call.SpawnPushServerInitialMetadata(std::move(metadata));
//       }
//       return Http2Status::Ok();
//     }
//     GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessMetadata Failed";
//     return
//     ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>>::TakeStatus(
//         std::move(read_result));
//   }
//   return Http2Status::Ok();
// }

// Http2Status Http2ServerTransport::ParseAndDiscardHeaders(
//     SliceBuffer&& buffer, const bool is_end_headers,
//     const RefCountedPtr<Stream> stream, Http2Status&& original_status,
//     DebugLocation whence) {
//   const bool is_initial_metadata = !incoming_headers_.HeaderHasEndStream();
//   const uint32_t incoming_stream_id = incoming_headers_.GetStreamId();
//   GRPC_HTTP2_SERVER_DLOG
//       << "Http2ServerTransport ParseAndDiscardHeaders buffer "
//          "size: "
//       << buffer.Length() << " is_initial_metadata: " << is_initial_metadata
//       << " is_end_headers: " << is_end_headers
//       << " incoming_stream_id: " << incoming_stream_id
//       << " stream_id: " << (stream == nullptr ? 0 : stream->GetStreamId())
//       << " original_status: " << original_status.DebugString()
//       << " whence: " << whence.file() << ":" << whence.line();

//   return http2::ParseAndDiscardHeaders(
//       parser_, std::move(buffer),
//       HeaderAssembler::ParseHeaderArgs{
//           /*is_initial_metadata=*/is_initial_metadata,
//           /*is_end_headers=*/is_end_headers,
//           /*is_client=*/true,
//           /*max_header_list_size_soft_limit=*/
//           incoming_headers_.soft_limit(),
//           /*max_header_list_size_hard_limit=*/
//           settings_->acked().max_header_list_size(),
//           /*stream_id=*/incoming_stream_id,
//       },
//       stream, std::move(original_status));
// }

Http2Status ProcessHttp2DataFrame(Http2DataFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-data
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2DataFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2DataFrame Promise { stream_id="
      << frame.stream_id << ", end_stream=" << frame.end_stream
      << ", payload length=" << frame.payload.Length() << "}";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2HeaderFrame(Http2HeaderFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-headers
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2HeaderFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2HeaderFrame Promise { stream_id="
      << frame.stream_id << ", end_headers=" << frame.end_headers
      << ", end_stream=" << frame.end_stream
      << ", payload length=" << frame.payload.Length() << " }";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2RstStreamFrame(Http2RstStreamFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-rst_stream
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2RstStreamFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2RstStreamFrame Promise{ stream_id="
      << frame.stream_id << ", error_code=" << frame.error_code << " }";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2SettingsFrame(Http2SettingsFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-settings
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2SettingsFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  // Load into this.settings_
  // Take necessary actions as per settings that have changed.
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2SettingsFrame Promise { ack="
      << frame.ack << ", settings length=" << frame.settings.size() << "}";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2PingFrame(Http2PingFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-ping
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2PingFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2PingFrame Promise { ack="
      << frame.ack << ", opaque=" << frame.opaque << " }";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2GoawayFrame(Http2GoawayFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-goaway
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2GoawayFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2GoawayFrame Promise { "
         "last_stream_id="
      << frame.last_stream_id << ", error_code=" << frame.error_code << "}";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2WindowUpdateFrame(Http2WindowUpdateFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-window_update
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2WindowUpdateFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2WindowUpdateFrame Promise { "
         " stream_id="
      << frame.stream_id << ", increment=" << frame.increment << "}";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2ContinuationFrame(Http2ContinuationFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-continuation
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2ContinuationFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2ContinuationFrame Promise { "
         "stream_id="
      << frame.stream_id << ", end_headers=" << frame.end_headers
      << ", payload length=" << frame.payload.Length() << " }";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2SecurityFrame(Http2SecurityFrame frame) {
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2SecurityFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2SecurityFrame Promise { payload "
         "length="
      << frame.payload.Length() << " }";
  return Http2Status::Ok();
}

auto Http2ServerTransport::ProcessOneIncomingFrame(Http2Frame frame) {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessOneFrame Factory";
  return AssertResultType<Http2Status>(MatchPromise(
      std::move(frame),
      [](Http2DataFrame frame) {
        return ProcessHttp2DataFrame(std::move(frame));
      },
      [](Http2HeaderFrame frame) {
        return ProcessHttp2HeaderFrame(std::move(frame));
      },
      [](Http2RstStreamFrame frame) {
        return ProcessHttp2RstStreamFrame(frame);
      },
      [](Http2SettingsFrame frame) {
        return ProcessHttp2SettingsFrame(std::move(frame));
      },
      [](Http2PingFrame frame) { return ProcessHttp2PingFrame(frame); },
      [](Http2GoawayFrame frame) {
        return ProcessHttp2GoawayFrame(std::move(frame));
      },
      [](Http2WindowUpdateFrame frame) {
        return ProcessHttp2WindowUpdateFrame(frame);
      },
      [](Http2ContinuationFrame frame) {
        return ProcessHttp2ContinuationFrame(std::move(frame));
      },
      [](Http2SecurityFrame frame) {
        return ProcessHttp2SecurityFrame(std::move(frame));
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

auto Http2ServerTransport::ReadAndProcessOneFrame() {
  GRPC_HTTP2_SERVER_DLOG
      << "Http2ServerTransport ReadAndProcessOneFrame Factory";
  return AssertResultType<absl::Status>(TrySeq(
      // Fetch the first kFrameHeaderSize bytes of the Frame, these contain
      // the frame header.
      endpoint_.ReadSlice(kFrameHeaderSize),
      // Parse the frame header.
      [](Slice header_bytes) -> Http2FrameHeader {
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport ReadAndProcessOneFrame Parse "
            << header_bytes.as_string_view();
        return Http2FrameHeader::Parse(header_bytes.begin());
      },
      // Read the payload of the frame.
      [this](Http2FrameHeader header) {
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport ReadAndProcessOneFrame Read";
        current_frame_header_ = header;
        return AssertResultType<absl::StatusOr<SliceBuffer>>(
            endpoint_.Read(header.length));
      },
      // Parse the payload of the frame based on frame type.
      [this](SliceBuffer payload) -> absl::StatusOr<Http2Frame> {
        GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport ReadAndProcessOneFrame "
                                  "ParseFramePayload payload length: "
                               << payload.Length();
        ValueOrHttp2Status<Http2Frame> frame =
            ParseFramePayload(current_frame_header_, std::move(payload));
        if (frame.IsOk()) {
          return TakeValue(std::move(frame));
        }

        return HandleError(
            ValueOrHttp2Status<Http2Frame>::TakeStatus(std::move(frame)));
      },
      [this](GRPC_UNUSED Http2Frame frame) {
        return Map(
            ProcessOneIncomingFrame(std::move(frame)),
            [self = RefAsSubclass<Http2ServerTransport>()](Http2Status status) {
              if (status.IsOk()) {
                return absl::OkStatus();
              }
              return self->HandleError(std::move(status));
            });
      }));
}

auto Http2ServerTransport::ReadLoop() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport ReadLoop Factory";
  return AssertResultType<absl::Status>(Loop([this]() {
    return TrySeq(ReadAndProcessOneFrame(), []() -> LoopCtl<absl::Status> {
      GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport ReadLoop Continue";
      return Continue();
    });
  }));
}

auto Http2ServerTransport::OnReadLoopEnded() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport OnReadLoopEnded Factory";
  return
      [self = RefAsSubclass<Http2ServerTransport>()](absl::Status status) {
        // TODO(tjagtap) : [PH2][P2] : Implement this.
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport OnReadLoopEnded Promise Status=" << status;
        GRPC_UNUSED absl::Status error_status =
            self->HandleError(Http2Status::AbslConnectionError(
                status.code(), std::string(status.message())));
      };
}

//////////////////////////////////////////////////////////////////////////////
// Transport Write Path

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

auto Http2ServerTransport::OnWriteLoopEnded() {
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport OnWriteLoopEnded Factory";
  return
      [self = RefAsSubclass<Http2ServerTransport>()](absl::Status status) {
        // TODO(tjagtap) : [PH2][P2] : Implement this.
        GRPC_HTTP2_SERVER_DLOG
            << "Http2ServerTransport OnWriteLoopEnded Promise Status="
            << status;
        GRPC_UNUSED absl::Status error_status =
            self->HandleError(Http2Status::AbslConnectionError(
                status.code(), std::string(status.message())));
      };
}

//////////////////////////////////////////////////////////////////////////////
// Spawn Helpers and Promise Helpers

//////////////////////////////////////////////////////////////////////////////
// Endpoint Helpers

//////////////////////////////////////////////////////////////////////////////
// Settings

// auto WaitForSettingsTimeoutOnDone();

// void MaybeSpawnWaitForSettingsTimeout();

// void EnforceLatestIncomingSettings();

//////////////////////////////////////////////////////////////////////////////
// Flow Control and BDP

// void ActOnFlowControlAction(...);

// void MaybeGetWindowUpdateFrames(SliceBuffer& output_buf);

// auto FlowControlPeriodicUpdateLoop();

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

//////////////////////////////////////////////////////////////////////////////
// Stream Operations

//////////////////////////////////////////////////////////////////////////////
// Ping Keepalive and Goaway

//////////////////////////////////////////////////////////////////////////////
// Error Path and Close Path

//////////////////////////////////////////////////////////////////////////////
// Misc Transport Stuff

//////////////////////////////////////////////////////////////////////////////
// Inner Classes and Structs

//////////////////////////////////////////////////////////////////////////////
// Constructor, Destructor etc.

Http2ServerTransport::Http2ServerTransport(
    PromiseEndpoint endpoint, GRPC_UNUSED const ChannelArgs& channel_args,
    std::shared_ptr<EventEngine> event_engine)
    : outgoing_frames_(kMpscSize),
      endpoint_(std::move(endpoint)),
      incoming_headers_(IncomingMetadataTracker::GetPeerString(endpoint_)),
      ping_manager_(std::nullopt),
      goaway_manager_(Http2ServerTransport::GoawayInterfaceImpl::Make(this)),
      memory_owner_(channel_args.GetObject<ResourceQuota>()
                        ->memory_quota()
                        ->CreateMemoryOwner()),
      flow_control_(
          "PH2_Server",
          channel_args.GetBool(GRPC_ARG_HTTP2_BDP_PROBE).value_or(true),
          &memory_owner_) {
  // TODO(tjagtap) : [PH2][P2] : Save and apply channel_args.
  // TODO(tjagtap) : [PH2][P2] : Initialize settings_ to appropriate values.

  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Constructor Begin";

  // Initialize the general party and write party.
  auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
  general_party_arena->SetContext<EventEngine>(event_engine.get());
  general_party_ = Party::Make(std::move(general_party_arena));

  general_party_->Spawn("ReadLoop", ReadLoop(), OnReadLoopEnded());
  general_party_->Spawn("WriteLoop", WriteLoop(), OnWriteLoopEnded());
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Constructor End";
}

Http2ServerTransport::~Http2ServerTransport() {
  // TODO(tjagtap) : [PH2][P2] : Implement the needed cleanup
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Destructor Begin";
  general_party_.reset();
  GRPC_HTTP2_SERVER_DLOG << "Http2ServerTransport Destructor End";
}

}  // namespace http2
}  // namespace grpc_core
