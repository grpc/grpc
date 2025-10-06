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
#include <grpc/support/port_platform.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/flow_control_manager.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/header_assembler.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_ztrace_collector.h"
#include "src/core/ext/transport/chttp2/transport/internal_channel_arg_names.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
#include "src/core/ext/transport/chttp2/transport/stream_data_queue.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/match_promise.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"

namespace grpc_core {
namespace http2 {

using grpc_event_engine::experimental::EventEngine;
using EnqueueResult = StreamDataQueue<ClientMetadataHandle>::EnqueueResult;

// Experimental : This is just the initial skeleton of class
// and it is functions. The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P3] : Delete this comment when http2
// rollout begins

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

void Http2ClientTransport::AbortWithError() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport AbortWithError Begin";
  // TODO(tjagtap) : [PH2][P2] : Implement this function.
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport AbortWithError End";
}

///////////////////////////////////////////////////////////////////////////////
// Processing each type of frame

Http2Status Http2ClientTransport::ProcessHttp2DataFrame(Http2DataFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-data
  GRPC_HTTP2_CLIENT_DLOG << "Http2Transport ProcessHttp2DataFrame { stream_id="
                         << frame.stream_id
                         << ", end_stream=" << frame.end_stream
                         << ", payload=" << frame.payload.JoinIntoString()
                         << "}";

  // TODO(akshitpatel) : [PH2][P3] : Investigate if we should do this even if
  // the function returns a non-ok status?
  ping_manager_.ReceivedDataFrame();

  // Lookup stream
  GRPC_HTTP2_CLIENT_DLOG << "Http2Transport ProcessHttp2DataFrame LookupStream";
  RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
  if (stream == nullptr) {
    // TODO(tjagtap) : [PH2][P2] : Implement the correct behaviour later.
    // RFC9113 : If a DATA frame is received whose stream is not in the "open"
    // or "half-closed (local)" state, the recipient MUST respond with a stream
    // error (Section 5.4.2) of type STREAM_CLOSED.
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2Transport ProcessHttp2DataFrame { stream_id="
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
      << "Http2Transport ProcessHttp2DataFrame AppendNewDataFrame";
  GrpcMessageAssembler& assembler = stream->assembler;
  Http2Status status =
      assembler.AppendNewDataFrame(frame.payload, frame.end_stream);
  if (!status.IsOk()) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2Transport ProcessHttp2DataFrame AppendNewDataFrame Failed";
    return status;
  }

  // Pass the messages up the stack if it is ready.
  while (true) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2Transport ProcessHttp2DataFrame ExtractMessage";
    ValueOrHttp2Status<MessageHandle> result = assembler.ExtractMessage();
    if (!result.IsOk()) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2Transport ProcessHttp2DataFrame ExtractMessage Failed";
      return ValueOrHttp2Status<MessageHandle>::TakeStatus(std::move(result));
    }
    MessageHandle message = TakeValue(std::move(result));
    if (message != nullptr) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2Transport ProcessHttp2DataFrame SpawnPushMessage "
          << message->DebugString();
      stream->call.SpawnPushMessage(std::move(message));
      continue;
    }
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2Transport ProcessHttp2DataFrame While Break";
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
      << "Http2Transport ProcessHttp2HeaderFrame Promise { stream_id="
      << frame.stream_id << ", end_headers=" << frame.end_headers
      << ", end_stream=" << frame.end_stream
      << ", payload=" << frame.payload.JoinIntoString() << " }";
  ping_manager_.ReceivedDataFrame();

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
        << "Http2Transport ProcessHttp2HeaderFrame Promise { stream_id="
        << frame.stream_id << "} Lookup Failed";
    return Http2Status::Ok();
  }
  if (stream->GetStreamState() == HttpStreamState::kHalfClosedRemote) {
    return Http2Status::Http2StreamError(
        Http2ErrorCode::kStreamClosed,
        std::string(RFC9113::kHalfClosedRemoteState));
  }

  incoming_header_in_progress_ = !frame.end_headers;
  incoming_header_stream_id_ = frame.stream_id;
  incoming_header_end_stream_ = frame.end_stream;
  if ((incoming_header_end_stream_ && stream->did_push_trailing_metadata) ||
      (!incoming_header_end_stream_ && stream->did_push_initial_metadata)) {
    return Http2Status::Http2StreamError(
        Http2ErrorCode::kInternalError,
        "gRPC Error : A gRPC server can send upto 1 initial metadata followed "
        "by upto 1 trailing metadata");
  }

  HeaderAssembler& assembler = stream->header_assembler;
  Http2Status append_result = assembler.AppendHeaderFrame(std::move(frame));
  if (append_result.IsOk()) {
    return ProcessMetadata(stream);
  }
  return append_result;
}

Http2Status Http2ClientTransport::ProcessMetadata(
    RefCountedPtr<Stream> stream) {
  HeaderAssembler& assembler = stream->header_assembler;
  CallHandler call = stream->call;

  GRPC_HTTP2_CLIENT_DLOG << "Http2Transport ProcessMetadata";
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
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2Transport ProcessMetadata SpawnPushServerTrailingMetadata";
        stream->MarkHalfClosedRemote();
        BeginCloseStream(stream,
                         /*reset_stream_error_code=*/std::nullopt,
                         std::move(metadata));
      } else {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2Transport ProcessMetadata SpawnPushServerInitialMetadata";
        stream->did_push_initial_metadata = true;
        call.SpawnPushServerInitialMetadata(std::move(metadata));
      }
      return Http2Status::Ok();
    }
    GRPC_HTTP2_CLIENT_DLOG << "Http2Transport ProcessMetadata Failed";
    return ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>>::TakeStatus(
        std::move(read_result));
  }
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessHttp2RstStreamFrame(
    Http2RstStreamFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-rst_stream
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2Transport ProcessHttp2RstStreamFrame { stream_id="
      << frame.stream_id << ", error_code=" << frame.error_code << " }";
  Http2ErrorCode error_code =
      RstFrameErrorCodeToHttp2ErrorCode(frame.error_code);
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

  GRPC_HTTP2_CLIENT_DLOG << "Http2Transport ProcessHttp2SettingsFrame { ack="
                         << frame.ack
                         << ", settings length=" << frame.settings.size()
                         << "}";

  // The connector code needs us to run this
  if (on_receive_settings_ != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, on_receive_settings_, absl::OkStatus());
    on_receive_settings_ = nullptr;
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
  } else {
    // Process the SETTINGS ACK Frame
    if (settings_.AckLastSend()) {
      transport_settings_.OnSettingsAckReceived();
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
  GRPC_HTTP2_CLIENT_DLOG << "Http2Transport ProcessHttp2PingFrame { ack="
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
  GRPC_HTTP2_CLIENT_DLOG << "Http2Transport ProcessHttp2GoawayFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  GRPC_HTTP2_CLIENT_DLOG << "Http2Transport ProcessHttp2GoawayFrame Promise { "
                            "last_stream_id="
                         << frame.last_stream_id
                         << ", error_code=" << frame.error_code
                         << ", debug_data=" << frame.debug_data.as_string_view()
                         << "}";
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessHttp2WindowUpdateFrame(
    Http2WindowUpdateFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-window_update
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2Transport ProcessHttp2WindowUpdateFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2Transport ProcessHttp2WindowUpdateFrame Promise { "
         " stream_id="
      << frame.stream_id << ", increment=" << frame.increment << "}";
  if (frame.stream_id != 0) {
    RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
    if (stream != nullptr) {
      chttp2::StreamFlowControl::OutgoingUpdateContext fc_update(
          &stream->flow_control);
      fc_update.RecvUpdate(frame.increment);
    }
  } else {
    chttp2::TransportFlowControl::OutgoingUpdateContext fc_update(
        &flow_control_);
    fc_update.RecvUpdate(frame.increment);
  }
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessHttp2ContinuationFrame(
    Http2ContinuationFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-continuation
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2Transport ProcessHttp2ContinuationFrame Promise { "
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
    return Http2Status::Ok();
  }
  if (stream->GetStreamState() == HttpStreamState::kHalfClosedRemote) {
    return Http2Status::Http2StreamError(
        Http2ErrorCode::kStreamClosed,
        std::string(RFC9113::kHalfClosedRemoteState));
  }

  HeaderAssembler& assember = stream->header_assembler;
  Http2Status result = assember.AppendContinuationFrame(std::move(frame));
  if (result.IsOk()) {
    return ProcessMetadata(stream);
  }
  return result;
}

Http2Status Http2ClientTransport::ProcessHttp2SecurityFrame(
    Http2SecurityFrame frame) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2Transport ProcessHttp2SecurityFrame "
                            "ProcessHttp2SecurityFrame { payload="
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

auto Http2ClientTransport::OnReadLoopEnded() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport OnReadLoopEnded Factory";
  return
      [self = RefAsSubclass<Http2ClientTransport>()](absl::Status status) {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport OnReadLoopEnded Promise Status=" << status;
        GRPC_UNUSED absl::Status error = self->HandleError(
            std::nullopt, Http2Status::AbslConnectionError(
                              status.code(), std::string(status.message())));
      };
}

// Equivalent to grpc_chttp2_act_on_flowctl_action in chttp2_transport.cc
// TODO(tjagtap) : [PH2][P4] : grpc_chttp2_act_on_flowctl_action has a "reason"
// parameter which looks like it would be really helpful for debugging. Add that
void Http2ClientTransport::ActOnFlowControlAction(
    const chttp2::FlowControlAction& action, const uint32_t stream_id) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::ActOnFlowControlAction";
  if (action.send_stream_update() != kNoActionNeeded) {
    GRPC_DCHECK_GT(stream_id, 0u);
    RefCountedPtr<Stream> stream = LookupStream(stream_id);
    if (GPR_LIKELY(stream != nullptr)) {
      const HttpStreamState state = stream->GetStreamState();
      if (state != HttpStreamState::kHalfClosedRemote &&
          state != HttpStreamState::kClosed) {
        // Stream is not remotely closed, so sending a WINDOW_UPDATE is
        // potentially useful.
        // TODO(tjagtap) : [PH2][P1] Plumb with flow control
      }
    } else {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport ActOnFlowControlAction stream is null";
    }
  }

  if (action.send_transport_update() != kNoActionNeeded) {
    // TODO(tjagtap) : [PH2][P1] Plumb with flow control
  }

  // TODO(tjagtap) : [PH2][P1] Plumb
  // enable_preferred_rx_crypto_frame_advertisement with settings
  ActOnFlowControlActionSettings(
      action, settings_.mutable_local(),
      /*enable_preferred_rx_crypto_frame_advertisement=*/true);

  if (action.AnyUpdateImmediately()) {
    TriggerWriteCycle();
  }
}

///////////////////////////////////////////////////////////////////////////////
// Write Related Promises and Promise Factories

auto Http2ClientTransport::WriteControlFrames() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport WriteControlFrames Factory";
  SliceBuffer output_buf;
  if (is_first_write_) {
    GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport Write "
                              "GRPC_CHTTP2_CLIENT_CONNECT_STRING";
    output_buf.Append(Slice(
        grpc_slice_from_copied_string(GRPC_CHTTP2_CLIENT_CONNECT_STRING)));
    is_first_write_ = false;
  }
  MaybeGetSettingsFrame(output_buf);
  ping_manager_.MaybeGetSerializedPingFrames(output_buf,
                                             NextAllowedPingInterval());
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
  ping_manager_.NotifyPingSent(ping_timeout_);
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
  // TODO(akshitpatel) : [PH2][P3] : Plug transport_tokens when
  // transport flow control is implemented.
  StreamDataQueue<ClientMetadataHandle>::DequeueResult result =
      stream->DequeueFrames(
          /*transport_tokens*/ std::min(
              std::numeric_limits<uint32_t>::max(),
              static_cast<uint32_t>(Clamp<size_t>(write_bytes_remaining_, 0,
                                                  RFC9113::kMaxSize31Bit - 1))),
          settings_.peer().max_frame_size(), encoder_);
  if (result.is_writable) {
    // Stream is still writable. Enqueue it back to the writable
    // stream list.
    // TODO(akshitpatel) : [PH2][P3] : Plug transport_tokens when
    // transport flow control is implemented.
    absl::Status status =
        writable_stream_list_.Enqueue(stream, result.priority);
    if (GPR_UNLIKELY(!status.ok())) {
      GRPC_HTTP2_CLIENT_DLOG
          << "Http2ClientTransport MultiplexerLoop Failed to "
             "enqueue stream "
          << stream->GetStreamId() << " with status: " << status;
      // Close transport if we fail to enqueue stream.
      return HandleError(std::nullopt, Http2Status::AbslConnectionError(
                                           absl::StatusCode::kUnavailable,
                                           std::string(status.message())));
    }
  }
  if (result.InitialMetadataDequeued()) {
    stream->SentInitialMetadata();
  }
  if (result.HalfCloseDequeued()) {
    stream->MarkHalfClosedLocal();
    CloseStream(stream, CloseStreamArgs{
                            /*close_reads=*/stream->did_push_trailing_metadata,
                            /*close_writes=*/true});
  }
  if (result.ResetStreamDequeued()) {
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
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport MultiplexerLoop "
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
            // TODO(akshitpatel) : [PH2][P3] : Plug transport_tokens when
            // transport flow control is implemented.
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
              // TODO(akshitpatel) : [PH2][P4] : We will waste a stream id in
              // the rare scenario where the stream is aborted before it can be
              // written to. This is a possible area to optimize in future.
              absl::Status status =
                  self->AssignStreamIdAndAddToStreamList(stream);
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

auto Http2ClientTransport::OnMultiplexerLoopEnded() {
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport OnMultiplexerLoopEnded Factory";
  return
      [self = RefAsSubclass<Http2ClientTransport>()](absl::Status status) {
        GRPC_HTTP2_CLIENT_DLOG
            << "Http2ClientTransport OnMultiplexerLoopEnded Promise Status="
            << status;
        GRPC_UNUSED absl::Status error = self->HandleError(
            std::nullopt, Http2Status::AbslConnectionError(
                              status.code(), std::string(status.message())));
      };
}

absl::Status Http2ClientTransport::AssignStreamIdAndAddToStreamList(
    RefCountedPtr<Stream> stream) {
  absl::StatusOr<uint32_t> next_stream_id = NextStreamId();
  if (!next_stream_id.ok()) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport AssignStreamIdAndAddToStreamList "
           "Failed to get next stream id for stream: "
        << stream.get();
    return std::move(next_stream_id).status();
  }
  GRPC_HTTP2_CLIENT_DLOG
      << "Http2ClientTransport AssignStreamIdAndAddToStreamList "
         "Assigned stream id: "
      << next_stream_id.value() << " to stream: " << stream.get();
  stream->SetStreamId(next_stream_id.value());
  {
    MutexLock lock(&transport_mutex_);
    stream_list_.emplace(next_stream_id.value(), stream);
  }
  return absl::OkStatus();
}

///////////////////////////////////////////////////////////////////////////////
// Constructor Destructor

Http2ClientTransport::Http2ClientTransport(
    PromiseEndpoint endpoint, GRPC_UNUSED const ChannelArgs& channel_args,
    std::shared_ptr<EventEngine> event_engine,
    grpc_closure* on_receive_settings)
    : channelz::DataSource(http2::CreateChannelzSocketNode(
          endpoint.GetEventEngineEndpoint(), channel_args)),
      endpoint_(std::move(endpoint)),
      next_stream_id_(/*Initial Stream ID*/ 1),
      should_reset_ping_clock_(false),
      incoming_header_in_progress_(false),
      incoming_header_end_stream_(false),
      is_first_write_(true),
      incoming_header_stream_id_(0),
      on_receive_settings_(on_receive_settings),
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
                    event_engine),
      keepalive_manager_(
          KeepAliveInterfaceImpl::Make(this),
          ((keepalive_timeout_ < ping_timeout_) ? keepalive_timeout_
                                                : Duration::Infinity()),
          keepalive_time_),
      keepalive_permit_without_calls_(
          channel_args.GetBool(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS)
              .value_or(false)),
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
      ztrace_collector_(std::make_shared<PromiseHttp2ZTraceCollector>()) {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport Constructor Begin";
  SourceConstructed();

  InitLocalSettings(settings_.mutable_local(), /*is_client=*/true);
  ReadSettingsFromChannelArgs(channel_args, settings_.mutable_local(),
                              flow_control_, /*is_client=*/true);

  // Initialize the general party and write party.
  auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
  general_party_arena->SetContext<EventEngine>(event_engine.get());
  general_party_ = Party::Make(std::move(general_party_arena));

  general_party_->Spawn("ReadLoop", UntilTransportClosed(ReadLoop()),
                        OnReadLoopEnded());
  general_party_->Spawn("MultiplexerLoop",
                        UntilTransportClosed(MultiplexerLoop()),
                        OnMultiplexerLoopEnded());
  // The keepalive loop is only spawned if the keepalive time is not infinity.
  keepalive_manager_.Spawn(general_party_.get());

  // TODO(tjagtap) : [PH2][P2] Delete this hack once flow control is done.
  // We are increasing the flow control window so that we can avoid sending
  // WINDOW_UPDATE frames while flow control is under development. Once it is
  // ready we should remove these lines.
  // <DeleteAfterFlowControl>
  Http2ErrorCode code = settings_.mutable_local().Apply(
      Http2Settings::kInitialWindowSizeWireId,
      (Http2Settings::max_initial_window_size() - 1));
  GRPC_DCHECK(code == Http2ErrorCode::kNoError);
  // </DeleteAfterFlowControl>

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
  general_party_->Spawn("SpawnFlushInitialFrames", TriggerWriteCycle(),
                        [](GRPC_UNUSED absl::Status status) {});

  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport Constructor End";
}

// This function MUST be idempotent. This function MUST be called from the
// transport party.
void Http2ClientTransport::CloseStream(RefCountedPtr<Stream> stream,
                                       CloseStreamArgs args,
                                       DebugLocation whence) {
  // TODO(akshitpatel) : [PH2][P3] : Measure the impact of holding mutex
  // throughout this function.
  MutexLock lock(&transport_mutex_);
  GRPC_DCHECK(stream != nullptr) << "stream is null";
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport::CloseStream for stream id: "
                         << stream->GetStreamId()
                         << " location=" << whence.file() << ":"
                         << whence.line();

  if (args.close_writes) {
    stream->SetWriteClosed();
  }

  if (args.close_reads) {
    GRPC_HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::CloseStream for stream id: "
        << stream->GetStreamId() << " closing stream for reads.";
    stream_list_.erase(stream->GetStreamId());
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
  state_tracker_.SetState(GRPC_CHANNEL_SHUTDOWN,
                          http2_status.GetAbslConnectionError(),
                          "transport closed");
  lock.Release();

  general_party_->Spawn(
      "CloseTransport",
      [self = RefAsSubclass<Http2ClientTransport>(),
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
                                 Http2ErrorCodeToRstFrameErrorCode(
                                     http2_status.GetConnectionErrorCode()),
                                 CancelledServerMetadataFromStatus(
                                     http2_status.GetAbslConnectionError()));
        }

        // RFC9113 : A GOAWAY frame might not immediately precede closing of
        // the connection; a receiver of a GOAWAY that has no more use for the
        // connection SHOULD still send a GOAWAY frame before terminating the
        // connection.
        // TODO(akshitpatel) : [PH2][P2] : There would a timer for sending
        // goaway here. Once goaway is sent or timer is expired, close the
        // transport.
        return Map(Immediate(absl::OkStatus()),
                   [self](GRPC_UNUSED absl::Status) mutable {
                     self->CloseTransport();
                     return Empty{};
                   });
      },
      [](Empty) {});
}

Http2ClientTransport::~Http2ClientTransport() {
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport Destructor Begin";
  GRPC_DCHECK(stream_list_.empty());
  memory_owner_.Reset();
  SourceDestructing();
  GRPC_HTTP2_CLIENT_DLOG << "Http2ClientTransport Destructor End";
}

void Http2ClientTransport::AddData(channelz::DataSink sink) {
  sink.AddData("Http2ClientTransport",
               channelz::PropertyList()
                   .Set("keepalive_time", keepalive_time_)
                   .Set("keepalive_timeout", keepalive_timeout_)
                   .Set("ping_timeout", ping_timeout_)
                   .Set("keepalive_permit_without_calls",
                        keepalive_permit_without_calls_));
  // TODO(tjagtap) : [PH2][P1] : These were causing data race.
  // Figure out how to plumb this.
  // .Set("settings", settings_.ChannelzProperties())
  // .Set("flow_control", flow_control_.stats().ChannelzProperties()));
  general_party_->ExportToChannelz("Http2ClientTransport Party", sink);
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
    MutexLock lock(&transport_mutex_);
    stream = MakeRefCounted<Stream>(
        call_handler, settings_.peer().allow_true_binary_metadata(),
        settings_.acked().allow_true_binary_metadata(), flow_control_);
  }
  const bool on_done_added = SetOnDone(call_handler, stream);
  if (!on_done_added) return std::nullopt;
  return stream;
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
