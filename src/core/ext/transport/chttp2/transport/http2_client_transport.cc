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

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/header_assembler.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/internal_channel_arg_names.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
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
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"

namespace grpc_core {
namespace http2 {

using grpc_event_engine::experimental::EventEngine;

// Experimental : This is just the initial skeleton of class
// and it is functions. The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P3] : Delete this comment when http2
// rollout begins

void Http2ClientTransport::PerformOp(grpc_transport_op* op) {
  // Notes : Refer : src/core/ext/transport/chaotic_good/client_transport.cc
  // Functions : StartConnectivityWatch, StopConnectivityWatch, PerformOp
  HTTP2_CLIENT_DLOG << "Http2ClientTransport PerformOp Begin";
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
  CHECK(op->set_accept_stream) << "Set_accept_stream not supported on clients";
  DCHECK(did_stuff) << "Unimplemented transport perform op ";

  ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
  HTTP2_CLIENT_DLOG << "Http2ClientTransport PerformOp End";
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
  HTTP2_CLIENT_DLOG << "Http2ClientTransport Orphan Begin";
  // Accessing general_party here is not advisable. It may so happen that
  // the party is already freed/may free up any time. The only guarantee here
  // is that the transport is still valid.
  MaybeSpawnCloseTransport(Http2Status::AbslConnectionError(
      absl::StatusCode::kUnavailable, "Orphaned"));
  Unref();
  HTTP2_CLIENT_DLOG << "Http2ClientTransport Orphan End";
}

void Http2ClientTransport::AbortWithError() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport AbortWithError Begin";
  // TODO(tjagtap) : [PH2][P2] : Implement this function.
  HTTP2_CLIENT_DLOG << "Http2ClientTransport AbortWithError End";
}

///////////////////////////////////////////////////////////////////////////////
// Processing each type of frame

Http2Status Http2ClientTransport::ProcessHttp2DataFrame(Http2DataFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-data
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2DataFrame { stream_id="
                       << frame.stream_id << ", end_stream=" << frame.end_stream
                       << ", payload=" << frame.payload.JoinIntoString() << "}";

  // TODO(akshitpatel) : [PH2][P3] : Investigate if we should do this even if
  // the function returns a non-ok status?
  ping_manager_.ReceivedDataFrame();

  // Lookup stream
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2DataFrame LookupStream";
  RefCountedPtr<Stream> stream = LookupStream(frame.stream_id);
  if (stream == nullptr) {
    // TODO(tjagtap) : [PH2][P2] : Implement the correct behaviour later.
    // RFC9113 : If a DATA frame is received whose stream is not in the "open"
    // or "half-closed (local)" state, the recipient MUST respond with a stream
    // error (Section 5.4.2) of type STREAM_CLOSED.
    HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2DataFrame { stream_id="
                         << frame.stream_id << "} Lookup Failed";
    return Http2Status::Ok();
  }

  // Add frame to assembler
  HTTP2_TRANSPORT_DLOG
      << "Http2Transport ProcessHttp2DataFrame AppendNewDataFrame";
  GrpcMessageAssembler& assembler = stream->assembler;
  Http2Status status =
      assembler.AppendNewDataFrame(frame.payload, frame.end_stream);
  if (!status.IsOk()) {
    HTTP2_TRANSPORT_DLOG
        << "Http2Transport ProcessHttp2DataFrame AppendNewDataFrame Failed";
    return status;
  }

  // Pass the messages up the stack if it is ready.
  while (true) {
    HTTP2_TRANSPORT_DLOG
        << "Http2Transport ProcessHttp2DataFrame ExtractMessage";
    ValueOrHttp2Status<MessageHandle> result = assembler.ExtractMessage();
    if (!result.IsOk()) {
      HTTP2_TRANSPORT_DLOG
          << "Http2Transport ProcessHttp2DataFrame ExtractMessage Failed";
      return ValueOrHttp2Status<MessageHandle>::TakeStatus(std::move(result));
    }
    MessageHandle message = TakeValue(std::move(result));
    if (message != nullptr) {
      // TODO(tjagtap) : [PH2][P1] : Ask ctiller what is the right way to plumb
      // with call V3. PushMessage or SpawnPushMessage.
      HTTP2_TRANSPORT_DLOG
          << "Http2Transport ProcessHttp2DataFrame SpawnPushMessage "
          << message->DebugString();
      stream->call.SpawnPushMessage(std::move(message));
      continue;
    }
    HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2DataFrame While Break";
    break;
  }

  // TODO(tjagtap) : [PH2][P1] : List of Tests:
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
  HTTP2_TRANSPORT_DLOG
      << "Http2Transport ProcessHttp2HeaderFrame Promise { stream_id="
      << frame.stream_id << ", end_headers=" << frame.end_headers
      << ", end_stream=" << frame.end_stream
      << ", payload=" << frame.payload.JoinIntoString() << " }";
  ping_manager_.ReceivedDataFrame();

  RefCountedPtr<Http2ClientTransport::Stream> stream =
      LookupStream(frame.stream_id);
  if (stream == nullptr) {
    // TODO(tjagtap) : [PH2][P3] : Implement this.
    // RFC9113 : The identifier of a newly established stream MUST be
    // numerically greater than all streams that the initiating endpoint has
    // opened or reserved. This governs streams that are opened using a HEADERS
    // frame and streams that are reserved using PUSH_PROMISE. An endpoint that
    // receives an unexpected stream identifier MUST respond with a connection
    // error (Section 5.4.1) of type PROTOCOL_ERROR.
    HTTP2_TRANSPORT_DLOG
        << "Http2Transport ProcessHttp2HeaderFrame Promise { stream_id="
        << frame.stream_id << "} Lookup Failed";
    return Http2Status::Ok();
  }

  incoming_header_in_progress_ = !frame.end_headers;
  incoming_header_stream_id_ = frame.stream_id;
  incoming_header_end_stream_ = frame.end_stream;
  if ((incoming_header_end_stream_ && stream->did_push_trailing_metadata) ||
      (!incoming_header_end_stream_ && stream->did_push_initial_metadata)) {
    return Http2Status::Http2StreamError(
        Http2ErrorCode::kInternalError,
        "gRPC Error : A gRPC server can send upto 1 intitial metadata followed "
        "by upto 1 trailing metadata");
  }

  HeaderAssembler& assember = stream->header_assembler;
  Http2Status append_result = assember.AppendHeaderFrame(std::move(frame));
  if (append_result.IsOk()) {
    return ProcessMetadata(stream->stream_id, assember, stream->call,
                           stream->did_push_initial_metadata,
                           stream->did_push_trailing_metadata);
  }
  return append_result;
}

Http2Status Http2ClientTransport::ProcessMetadata(
    uint32_t stream_id, HeaderAssembler& assember, CallHandler& call,
    bool& did_push_initial_metadata, bool& did_push_trailing_metadata) {
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessMetadata";
  if (assember.IsReady()) {
    ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>> read_result =
        assember.ReadMetadata(parser_, !incoming_header_end_stream_,
                              /*is_client=*/true);
    if (read_result.IsOk()) {
      Arena::PoolPtr<grpc_metadata_batch> metadata =
          TakeValue(std::move(read_result));
      if (incoming_header_end_stream_) {
        // TODO(tjagtap) : [PH2][P1] : Is this the right way to differentiate
        // between initial and trailing metadata?
        HTTP2_TRANSPORT_DLOG
            << "Http2Transport ProcessMetadata SpawnPushServerTrailingMetadata";
        did_push_trailing_metadata = true;
        call.SpawnPushServerTrailingMetadata(std::move(metadata));
        CloseStream(stream_id, absl::OkStatus(),
                    CloseStreamArgs{
                        /*close_reads=*/true,
                        /*close_writes=*/true,
                        /*send_rst_stream=*/false,
                        /*should_not_push_trailers=*/true,
                    });

      } else {
        HTTP2_TRANSPORT_DLOG
            << "Http2Transport ProcessMetadata SpawnPushServerInitialMetadata";
        did_push_initial_metadata = true;
        call.SpawnPushServerInitialMetadata(std::move(metadata));
      }
      return Http2Status::Ok();
    }
    HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessMetadata Failed";
    return ValueOrHttp2Status<Arena::PoolPtr<grpc_metadata_batch>>::TakeStatus(
        std::move(read_result));
  }
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessHttp2RstStreamFrame(
    Http2RstStreamFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-rst_stream
  HTTP2_TRANSPORT_DLOG
      << "Http2Transport ProcessHttp2RstStreamFrame { stream_id="
      << frame.stream_id << ", error_code=" << frame.error_code << " }";

  // TODO(akshitpatel) : [PH2][P2] : This would fail in case of a rst frame
  // with NoError. Handle this case.
  Http2Status status = Http2Status::Http2StreamError(
      GetErrorCodeFromRstFrameErrorCode(frame.error_code),
      "Reset stream frame received.");
  CloseStream(frame.stream_id, status.GetAbslStreamError(),
              CloseStreamArgs{
                  /*close_reads=*/true,
                  /*close_writes=*/true,
                  /*send_rst_stream=*/false,
                  /*push_trailing_metadata=*/true,
              });
  // In case of stream error, we do not want the Read Loop to be broken. Hence
  // returning an ok status.
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessHttp2SettingsFrame(
    Http2SettingsFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-settings
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2SettingsFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  // Load into this.settings_
  // Take necessary actions as per settings that have changed.
  HTTP2_TRANSPORT_DLOG
      << "Http2Transport ProcessHttp2SettingsFrame Promise { ack=" << frame.ack
      << ", settings length=" << frame.settings.size() << "}";
  return Http2Status::Ok();
}

auto Http2ClientTransport::ProcessHttp2PingFrame(Http2PingFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-ping
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2PingFrame { ack="
                       << frame.ack << ", opaque=" << frame.opaque << " }";
  return AssertResultType<Http2Status>(If(
      frame.ack,
      [self = RefAsSubclass<Http2ClientTransport>(), opaque = frame.opaque]() {
        // Received a ping ack.
        if (!self->ping_manager_.AckPing(opaque)) {
          HTTP2_TRANSPORT_DLOG << "Unknown ping resoponse received for ping id="
                               << opaque;
        }
        return Immediate(Http2Status::Ok());
      },
      [self = RefAsSubclass<Http2ClientTransport>(), opaque = frame.opaque]() {
        // TODO(akshitpatel) : [PH2][P2] : Have a counter to track number of
        // pending induced frames (Ping/Settings Ack). This is to ensure that
        // if write is taking a long time, we can stop reads and prioritize
        // writes.
        // RFC9113: PING responses SHOULD be given higher priority than any
        // other frame.
        self->pending_ping_acks_.push_back(opaque);
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
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2GoawayFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2GoawayFrame Promise { "
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
  HTTP2_TRANSPORT_DLOG
      << "Http2Transport ProcessHttp2WindowUpdateFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  HTTP2_TRANSPORT_DLOG
      << "Http2Transport ProcessHttp2WindowUpdateFrame Promise { "
         " stream_id="
      << frame.stream_id << ", increment=" << frame.increment << "}";
  return Http2Status::Ok();
}

Http2Status Http2ClientTransport::ProcessHttp2ContinuationFrame(
    Http2ContinuationFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-continuation
  HTTP2_TRANSPORT_DLOG
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

  HeaderAssembler& assember = stream->header_assembler;
  Http2Status result = assember.AppendContinuationFrame(std::move(frame));
  if (result.IsOk()) {
    return ProcessMetadata(stream->stream_id, assember, stream->call,
                           stream->did_push_initial_metadata,
                           stream->did_push_trailing_metadata);
  }
  return result;
}

Http2Status Http2ClientTransport::ProcessHttp2SecurityFrame(
    Http2SecurityFrame frame) {
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2SecurityFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  HTTP2_TRANSPORT_DLOG
      << "Http2Transport ProcessHttp2SecurityFrame Promise { payload="
      << frame.payload.JoinIntoString() << " }";
  return Http2Status::Ok();
}

auto Http2ClientTransport::ProcessOneFrame(Http2Frame frame) {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessOneFrame Factory";
  return AssertResultType<Http2Status>(MatchPromise(
      std::move(frame),
      [this](Http2DataFrame frame) {
        return ProcessHttp2DataFrame(std::move(frame));
      },
      [this](Http2HeaderFrame frame) {
        return ProcessHttp2HeaderFrame(std::move(frame));
      },
      [this](Http2RstStreamFrame frame) {
        return ProcessHttp2RstStreamFrame(frame);
      },
      [this](Http2SettingsFrame frame) {
        return ProcessHttp2SettingsFrame(std::move(frame));
      },
      [this](Http2PingFrame frame) { return ProcessHttp2PingFrame(frame); },
      [this](Http2GoawayFrame frame) {
        return ProcessHttp2GoawayFrame(std::move(frame));
      },
      [this](Http2WindowUpdateFrame frame) {
        return ProcessHttp2WindowUpdateFrame(frame);
      },
      [this](Http2ContinuationFrame frame) {
        return ProcessHttp2ContinuationFrame(std::move(frame));
      },
      [this](Http2SecurityFrame frame) {
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

///////////////////////////////////////////////////////////////////////////////
// Read Related Promises and Promise Factories

auto Http2ClientTransport::ReadAndProcessOneFrame() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport ReadAndProcessOneFrame Factory";
  return AssertResultType<absl::Status>(TrySeq(
      // Fetch the first kFrameHeaderSize bytes of the Frame, these contain
      // the frame header.
      EndpointReadSlice(kFrameHeaderSize),
      // Parse the frame header.
      [](Slice header_bytes) -> Http2FrameHeader {
        HTTP2_CLIENT_DLOG
            << "Http2ClientTransport ReadAndProcessOneFrame Parse "
            << header_bytes.as_string_view();
        return Http2FrameHeader::Parse(header_bytes.begin());
      },
      // Validate the incoming frame as per the current state of the transport
      [this](Http2FrameHeader header) {
        if (incoming_header_in_progress_ &&
            (current_frame_header_.type != 9 /*Continuation*/ ||
             current_frame_header_.stream_id != incoming_header_stream_id_)) {
          LOG(ERROR) << "Closing Connection " << header.ToString() << " "
                     << kAssemblerContiguousSequenceError;
          return HandleError(Http2Status::Http2ConnectionError(
              Http2ErrorCode::kProtocolError,
              std::string(kAssemblerContiguousSequenceError)));
        }
        HTTP2_CLIENT_DLOG << "Http2ClientTransport ReadAndProcessOneFrame "
                             "Validated Frame Header:"
                          << header.ToString();
        current_frame_header_ = header;
        return absl::OkStatus();
      },
      // Read the payload of the frame.
      [this]() {
        HTTP2_CLIENT_DLOG
            << "Http2ClientTransport ReadAndProcessOneFrame Read Frame ";
        return AssertResultType<absl::StatusOr<SliceBuffer>>(
            EndpointRead(current_frame_header_.length));
      },
      // Parse the payload of the frame based on frame type.
      [this](SliceBuffer payload) -> absl::StatusOr<Http2Frame> {
        HTTP2_CLIENT_DLOG
            << "Http2ClientTransport ReadAndProcessOneFrame ParseFramePayload "
            << payload.JoinIntoString();
        ValueOrHttp2Status<Http2Frame> frame =
            ParseFramePayload(current_frame_header_, std::move(payload));
        if (!frame.IsOk()) {
          return HandleError(
              ValueOrHttp2Status<Http2Frame>::TakeStatus(std::move(frame)));
        }
        return TakeValue(std::move(frame));
      },
      [this](GRPC_UNUSED Http2Frame frame) {
        HTTP2_CLIENT_DLOG
            << "Http2ClientTransport ReadAndProcessOneFrame ProcessOneFrame";
        return AssertResultType<absl::Status>(Map(
            ProcessOneFrame(std::move(frame)),
            [self = RefAsSubclass<Http2ClientTransport>()](Http2Status status) {
              if (!status.IsOk()) {
                return self->HandleError(std::move(status));
              }
              return absl::OkStatus();
            }));
      }));
}

auto Http2ClientTransport::ReadLoop() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport ReadLoop Factory";
  return AssertResultType<absl::Status>(Loop([this]() {
    return TrySeq(ReadAndProcessOneFrame(), []() -> LoopCtl<absl::Status> {
      HTTP2_CLIENT_DLOG << "Http2ClientTransport ReadLoop Continue";
      return Continue();
    });
  }));
}

auto Http2ClientTransport::OnReadLoopEnded() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport OnReadLoopEnded Factory";
  return [self = RefAsSubclass<Http2ClientTransport>()](absl::Status status) {
    // TODO(akshitpatel) : [PH2][P1] : Implement this.
    HTTP2_CLIENT_DLOG << "Http2ClientTransport OnReadLoopEnded Promise Status="
                      << status;
    GRPC_UNUSED absl::Status error =
        self->HandleError(Http2Status::AbslConnectionError(
            status.code(), std::string(status.message())));
  };
}

///////////////////////////////////////////////////////////////////////////////
// Write Related Promises and Promise Factories

auto Http2ClientTransport::WriteFromQueue() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport WriteFromQueue Factory";
  return TrySeq(
      outgoing_frames_.NextBatch(128),
      [self = RefAsSubclass<Http2ClientTransport>()](
          std::vector<Http2Frame> frames) {
        SliceBuffer output_buf;
        Serialize(absl::Span<Http2Frame>(frames), output_buf);
        uint64_t buffer_length = output_buf.Length();
        HTTP2_CLIENT_DLOG << "Http2ClientTransport WriteFromQueue Promise";
        return If(
            buffer_length > 0,
            [self, output_buffer = std::move(output_buf)]() mutable {
              self->bytes_sent_in_last_write_ = true;
              return self->endpoint_.Write(std::move(output_buffer),
                                           PromiseEndpoint::WriteArgs{});
            },
            [] { return absl::OkStatus(); });
      });
}

auto Http2ClientTransport::WriteLoop() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport WriteLoop Factory";
  return AssertResultType<absl::Status>(Loop([this]() {
    // TODO(akshitpatel) : [PH2][P1] : Once a common SliceBuffer is used, we
    // can move bytes_sent_in_last_write_ to be a local variable.
    bytes_sent_in_last_write_ = false;
    return TrySeq(
        // TODO(akshitpatel) : [PH2][P1] : WriteFromQueue may write settings
        // acks as well. This will break the call to ResetPingClock as it only
        // needs to be called on writing Data/Header/WindowUpdate frames.
        // Possible fixes: Either WriteFromQueue iterates over all the frames
        // and figures out the types of frames needed (this may anyways be
        // needed to check that we do not send frames for closed streams) or we
        // have flags to indicate the types of frame that are enqueued.
        WriteFromQueue(),
        [self = RefAsSubclass<Http2ClientTransport>()] {
          return self->MaybeSendPing();
        },
        [self = RefAsSubclass<Http2ClientTransport>()] {
          return self->MaybeSendPingAcks();
        },
        [this]() -> LoopCtl<absl::Status> {
          // If any Header/Data/WindowUpdate frame was sent in the last write,
          // reset the ping clock.
          if (bytes_sent_in_last_write_) {
            ping_manager_.ResetPingClock(/*is_client=*/true);
          }
          HTTP2_CLIENT_DLOG << "Http2ClientTransport WriteLoop Continue";
          return Continue();
        });
  }));
}

auto Http2ClientTransport::OnWriteLoopEnded() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport OnWriteLoopEnded Factory";
  return [self = RefAsSubclass<Http2ClientTransport>()](absl::Status status) {
    // TODO(tjagtap) : [PH2][P1] : Implement this.
    HTTP2_CLIENT_DLOG << "Http2ClientTransport OnWriteLoopEnded Promise Status="
                      << status;
    GRPC_UNUSED absl::Status error =
        self->HandleError(Http2Status::AbslConnectionError(
            status.code(), std::string(status.message())));
  };
}

///////////////////////////////////////////////////////////////////////////////
// Constructor Destructor

Http2ClientTransport::Http2ClientTransport(
    PromiseEndpoint endpoint, GRPC_UNUSED const ChannelArgs& channel_args,
    std::shared_ptr<EventEngine> event_engine)
    : endpoint_(std::move(endpoint)),
      outgoing_frames_(kMpscSize),
      stream_id_mutex_(/*Initial Stream Id*/ 1),
      bytes_sent_in_last_write_(false),
      incoming_header_in_progress_(false),
      incoming_header_end_stream_(false),
      incoming_header_stream_id_(0),
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
      keepalive_permit_without_calls_(false) {
  // TODO(tjagtap) : [PH2][P2] : Save and apply channel_args.
  // TODO(tjagtap) : [PH2][P2] : Initialize settings_ to appropriate values.

  HTTP2_CLIENT_DLOG << "Http2ClientTransport Constructor Begin";

  // Initialize the general party and write party.
  auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
  general_party_arena->SetContext<EventEngine>(event_engine.get());
  general_party_ = Party::Make(std::move(general_party_arena));

  general_party_->Spawn("ReadLoop", ReadLoop(), OnReadLoopEnded());
  // TODO(tjagtap) : [PH2][P2] Fix when needed.
  general_party_->Spawn("WriteLoop", WriteLoop(), OnWriteLoopEnded());

  // The keepalive loop is only spawned if the keepalive time is not infinity.
  keepalive_manager_.Spawn(general_party_.get());

  // TODO(tjagtap) : [PH2][P2] Fix Settings workflow.
  std::optional<Http2SettingsFrame> settings_frame =
      settings_.MaybeSendUpdate();
  if (settings_frame.has_value()) {
    general_party_->Spawn(
        "SendFirstSettingsFrame",
        [self = RefAsSubclass<Http2ClientTransport>(),
         frame = std::move(*settings_frame)]() mutable {
          return self->EnqueueOutgoingFrame(std::move(frame));
        },
        [](GRPC_UNUSED absl::Status status) {});
  }
  HTTP2_CLIENT_DLOG << "Http2ClientTransport Constructor End";
}

void Http2ClientTransport::CloseTransport() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport::CloseTransport";

  // This is the only place where the general_party_ is
  // reset.
  general_party_.reset();
}

void Http2ClientTransport::MaybeSpawnCloseTransport(Http2Status http2_status,
                                                    DebugLocation whence) {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport::MaybeSpawnCloseTransport "
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
  HTTP2_CLIENT_DLOG << "Http2ClientTransport::MaybeSpawnCloseTransport "
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
        HTTP2_CLIENT_DLOG
            << "Http2ClientTransport::CloseTransport Cleaning up call stacks";
        // Clean up the call stacks for all active streams.
        for (const auto& pair : stream_list) {
          // There is no merit in transitioning the stream to
          // closed state here as the subsequent lookups would
          // fail. Also, as this is running on the transport
          // party, there would not be concurrent access to the stream.
          auto& stream = pair.second;
          stream->call.SpawnPushServerTrailingMetadata(
              ServerMetadataFromStatus(http2_status.GetAbslConnectionError()));
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
  // TODO(tjagtap) : [PH2][P1] : Implement the needed cleanup
  HTTP2_CLIENT_DLOG << "Http2ClientTransport Destructor Begin";
  DCHECK(stream_list_.empty());
  HTTP2_CLIENT_DLOG << "Http2ClientTransport Destructor End";
}

///////////////////////////////////////////////////////////////////////////////
// Stream Related Operations

RefCountedPtr<Http2ClientTransport::Stream> Http2ClientTransport::LookupStream(
    uint32_t stream_id) {
  MutexLock lock(&transport_mutex_);
  auto it = stream_list_.find(stream_id);
  if (it == stream_list_.end()) {
    HTTP2_CLIENT_DLOG
        << "Http2ClientTransport::LookupStream Stream not found stream_id="
        << stream_id;
    return nullptr;
  }
  return it->second;
}

bool Http2ClientTransport::MakeStream(CallHandler call_handler,
                                      const uint32_t stream_id) {
  // https://datatracker.ietf.org/doc/html/rfc9113#name-stream-identifiers
  // TODO(tjagtap) : [PH2][P2] Validate implementation.

  // TODO(akshitpatel) : [PH2][P1] : Probably do not need this lock. This
  // function is always called under the stream_id_mutex_. The issue is the
  // OnDone needs to be synchronous and hence InterActivityMutex might not be
  // an option to protect the stream_list_.
  MutexLock lock(&transport_mutex_);
  const bool on_done_added =
      call_handler.OnDone([self = RefAsSubclass<Http2ClientTransport>(),
                           stream_id](bool cancelled) {
        HTTP2_CLIENT_DLOG << "PH2: Client call " << self.get()
                          << " id=" << stream_id
                          << " done: cancelled=" << cancelled;
        if (cancelled) {
          // TODO(akshitpatel) : [PH2][P2] : There are two ways to handle
          // cancellation.
          // 1. Call CloseStream from the on_done callback as done here. This
          //    will be invoked when PullServerTrailingMetadata resolves.
          // 2. Call CloseStream from the OutboundLoop. When the call is
          //    cancelled, for_each() should return with an error. The
          //    WasCancelled() function can be used to determinie if the call
          //    was cancelled.
          // At this point, both the above mentioned approaches seem to be more
          // or less the same as both are running on the call party.
          self->CloseStream(stream_id, absl::CancelledError(),
                            CloseStreamArgs{
                                /*close_reads=*/true,
                                /*close_writes=*/true,
                                /*send_rst_stream=*/true,
                                /*push_trailing_metadata=*/false,
                            });
        }
      });
  if (!on_done_added) return false;
  stream_list_.emplace(
      stream_id, MakeRefCounted<Stream>(std::move(call_handler), stream_id));
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Call Spine related operations

auto Http2ClientTransport::CallOutboundLoop(
    CallHandler call_handler, const uint32_t stream_id,
    InterActivityMutex<uint32_t>::Lock lock /* Locked stream_id_mutex */,
    ClientMetadataHandle metadata) {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport CallOutboundLoop";

  // Convert a message to a Http2DataFrame and send the frame out.
  auto send_message = [self = RefAsSubclass<Http2ClientTransport>(),
                       stream_id](MessageHandle message) mutable {
    // TODO(akshitpatel) : [PH2][P2] : Assuming one message per frame.
    // This will eventually change as more logic is added.
    SliceBuffer frame_payload;
    size_t payload_size = message->payload()->Length();
    AppendGrpcHeaderToSliceBuffer(frame_payload, message->flags(),
                                  payload_size);
    frame_payload.TakeAndAppend(*message->payload());
    Http2DataFrame frame{stream_id, /*end_stream*/ false,
                         std::move(frame_payload)};
    HTTP2_CLIENT_DLOG << "Http2ClientTransport CallOutboundLoop send_message";
    return self->EnqueueOutgoingFrame(std::move(frame));
  };

  SliceBuffer buf;
  encoder_.EncodeRawHeaders(*metadata.get(), buf);
  Http2Frame frame = Http2HeaderFrame{stream_id, /*end_headers*/ true,
                                      /*end_stream*/ false, std::move(buf)};
  return GRPC_LATENT_SEE_PROMISE(
      "Ph2CallOutboundLoop",
      TrySeq(
          Map(EnqueueOutgoingFrame(std::move(frame)),
              [self = RefAsSubclass<Http2ClientTransport>(),
               stream_id](absl::Status status) {
                if (status.ok()) {
                  // TODO(akshitpatel) : [PH2][P3] : Investigate if stream
                  // lookup can be done once outside the promise and all the
                  // promises can hold a reference to the stream.
                  auto stream = self->LookupStream(stream_id);
                  if (GPR_UNLIKELY(stream == nullptr)) {
                    LOG(ERROR)
                        << "Stream not found while sending initial metadata";
                    return absl::InternalError(
                        "Stream not found while sending initial metadata");
                  }
                  stream->SentInitialMetadata();
                }
                return status;
              }),
          [call_handler, send_message, lock = std::move(lock)]() {
            // The lock will be released once the promise is constructed from
            // this factory. ForEach will be polled after the lock is
            // released.
            return ForEach(MessagesFrom(call_handler), send_message);
          },
          [self = RefAsSubclass<Http2ClientTransport>(), stream_id]() mutable {
            // TODO(akshitpatel): [PH2][P2] : Figure out a way to send the end
            // of stream frame in the same frame as the last message.
            Http2DataFrame frame{stream_id, /*end_stream*/ true, SliceBuffer()};
            return self->EnqueueOutgoingFrame(std::move(frame));
          },
          [call_handler]() mutable {
            return Map(call_handler.WasCancelled(), [](bool cancelled) {
              HTTP2_CLIENT_DLOG << "Http2ClientTransport PH2CallOutboundLoop"
                                   " End with cancelled="
                                << cancelled;
              return (cancelled) ? absl::CancelledError() : absl::OkStatus();
            });
          }));
}

void Http2ClientTransport::StartCall(CallHandler call_handler) {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport StartCall Begin";
  call_handler.SpawnGuarded(
      "OutboundLoop",
      TrySeq(
          call_handler.PullClientInitialMetadata(),
          [self = RefAsSubclass<Http2ClientTransport>()](
              ClientMetadataHandle metadata) {
            // Lock the stream_id_mutex_
            return Staple(self->stream_id_mutex_.Acquire(),
                          std::move(metadata));
          },
          [self = RefAsSubclass<Http2ClientTransport>(),
           call_handler](auto args /* Locked stream_id_mutex */) mutable {
            // TODO (akshitpatel) : [PH2][P2] :
            // Check for max concurrent streams.
            const uint32_t stream_id = self->NextStreamId(std::get<0>(args));
            return If(
                self->MakeStream(call_handler, stream_id),
                [self, call_handler, stream_id,
                 args = std::move(args)]() mutable {
                  return Map(
                      self->CallOutboundLoop(call_handler, stream_id,
                                             std::move(std::get<0>(args)),
                                             std::move(std::get<1>(args))),
                      [](absl::Status status) { return status; });
                },
                []() { return absl::InternalError("Failed to make stream"); });
          }));
  HTTP2_CLIENT_DLOG << "Http2ClientTransport StartCall End";
}

}  // namespace http2
}  // namespace grpc_core
