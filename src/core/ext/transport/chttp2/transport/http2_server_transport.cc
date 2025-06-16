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
#include <grpc/support/port_platform.h>

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "src/core/call/call_destination.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace_impl.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/match_promise.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted.h"

namespace grpc_core {
namespace http2 {

// TODO(tjagtap) : [PH2][P2] : Consider moving to common code.
#define HTTP2_SERVER_DLOG \
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

void Http2ServerTransport::SetCallDestination(
    RefCountedPtr<UnstartedCallDestination> call_destination) {
  // TODO(tjagtap) : [PH2][P2] : Implement this function.
  CHECK(call_destination_ == nullptr);
  CHECK(call_destination != nullptr);
  call_destination_ = call_destination;
  // got_acceptor_.Set(); // Copied from CG. Understand and fix.
}

void Http2ServerTransport::PerformOp(GRPC_UNUSED grpc_transport_op*) {
  HTTP2_SERVER_DLOG << "Http2ServerTransport PerformOp Begin";
  // TODO(tjagtap) : [PH2][P2] : Implement this function.
  HTTP2_SERVER_DLOG << "Http2ServerTransport PerformOp End";
}

void Http2ServerTransport::Orphan() {
  HTTP2_SERVER_DLOG << "Http2ServerTransport Orphan Begin";
  // TODO(tjagtap) : [PH2][P2] : Implement the needed cleanup
  general_party_.reset();
  Unref();
  HTTP2_SERVER_DLOG << "Http2ServerTransport Orphan End";
}

void Http2ServerTransport::AbortWithError() {
  HTTP2_SERVER_DLOG << "Http2ServerTransport AbortWithError Begin";
  // TODO(tjagtap) : [PH2][P2] : Implement this function.
  HTTP2_SERVER_DLOG << "Http2ServerTransport AbortWithError End";
}

Http2Status ProcessHttp2DataFrame(Http2DataFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-data
  HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessHttp2DataFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2DataFrame Promise { stream_id="
      << frame.stream_id << ", end_stream=" << frame.end_stream
      << ", payload=" << frame.payload.JoinIntoString() << "}";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2HeaderFrame(Http2HeaderFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-headers
  HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessHttp2HeaderFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2HeaderFrame Promise { stream_id="
      << frame.stream_id << ", end_headers=" << frame.end_headers
      << ", end_stream=" << frame.end_stream
      << ", payload=" << frame.payload.JoinIntoString() << " }";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2RstStreamFrame(Http2RstStreamFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-rst_stream
  HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2RstStreamFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2RstStreamFrame Promise{ stream_id="
      << frame.stream_id << ", error_code=" << frame.error_code << " }";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2SettingsFrame(Http2SettingsFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-settings
  HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessHttp2SettingsFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  // Load into this.settings_
  // Take necessary actions as per settings that have changed.
  HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2SettingsFrame Promise { ack="
      << frame.ack << ", settings length=" << frame.settings.size() << "}";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2PingFrame(Http2PingFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-ping
  HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessHttp2PingFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2PingFrame Promise { ack="
      << frame.ack << ", opaque=" << frame.opaque << " }";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2GoawayFrame(Http2GoawayFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-goaway
  HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessHttp2GoawayFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessHttp2GoawayFrame Promise { "
                       "last_stream_id="
                    << frame.last_stream_id
                    << ", error_code=" << frame.error_code
                    << ", debug_data=" << frame.debug_data.as_string_view()
                    << "}";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2WindowUpdateFrame(Http2WindowUpdateFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-window_update
  HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2WindowUpdateFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2WindowUpdateFrame Promise { "
         " stream_id="
      << frame.stream_id << ", increment=" << frame.increment << "}";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2ContinuationFrame(Http2ContinuationFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-continuation
  HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2ContinuationFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2ContinuationFrame Promise { "
         "stream_id="
      << frame.stream_id << ", end_headers=" << frame.end_headers
      << ", payload=" << frame.payload.JoinIntoString() << " }";
  return Http2Status::Ok();
}

Http2Status ProcessHttp2SecurityFrame(Http2SecurityFrame frame) {
  HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessHttp2SecurityFrame Factory";
  // TODO(tjagtap) : [PH2][P2] : Implement this.
  HTTP2_SERVER_DLOG
      << "Http2ServerTransport ProcessHttp2SecurityFrame Promise { payload="
      << frame.payload.JoinIntoString() << " }";
  return Http2Status::Ok();
}

auto Http2ServerTransport::ProcessOneFrame(Http2Frame frame) {
  HTTP2_SERVER_DLOG << "Http2ServerTransport ProcessOneFrame Factory";
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
  HTTP2_SERVER_DLOG << "Http2ServerTransport ReadAndProcessOneFrame Factory";
  return AssertResultType<absl::Status>(TrySeq(
      // Fetch the first kFrameHeaderSize bytes of the Frame, these contain
      // the frame header.
      endpoint_.ReadSlice(kFrameHeaderSize),
      // Parse the frame header.
      [](Slice header_bytes) -> Http2FrameHeader {
        HTTP2_SERVER_DLOG
            << "Http2ServerTransport ReadAndProcessOneFrame Parse "
            << header_bytes.as_string_view();
        return Http2FrameHeader::Parse(header_bytes.begin());
      },
      // Read the payload of the frame.
      [this](Http2FrameHeader header) {
        HTTP2_SERVER_DLOG << "Http2ServerTransport ReadAndProcessOneFrame Read";
        current_frame_header_ = header;
        return AssertResultType<absl::StatusOr<SliceBuffer>>(
            endpoint_.Read(header.length));
      },
      // Parse the payload of the frame based on frame type.
      [this](SliceBuffer payload) -> absl::StatusOr<Http2Frame> {
        HTTP2_SERVER_DLOG
            << "Http2ServerTransport ReadAndProcessOneFrame ParseFramePayload "
            << payload.JoinIntoString();
        ValueOrHttp2Status<Http2Frame> frame =
            ParseFramePayload(current_frame_header_, std::move(payload));
        if (frame.IsOk()) {
          return TakeValue(std::move(frame));
        }

        return HandleError(
            ValueOrHttp2Status<Http2Frame>::TakeStatus(std::move(frame)));
      },
      [this](GRPC_UNUSED Http2Frame frame) {
        HTTP2_SERVER_DLOG
            << "Http2ServerTransport ReadAndProcessOneFrame ProcessOneFrame";
        return Map(
            ProcessOneFrame(std::move(frame)),
            [self = RefAsSubclass<Http2ServerTransport>()](Http2Status status) {
              if (status.IsOk()) {
                return absl::OkStatus();
              }
              return self->HandleError(std::move(status));
            });
      }));
}

auto Http2ServerTransport::ReadLoop() {
  HTTP2_SERVER_DLOG << "Http2ServerTransport ReadLoop Factory";
  return AssertResultType<absl::Status>(Loop([this]() {
    return TrySeq(ReadAndProcessOneFrame(), []() -> LoopCtl<absl::Status> {
      HTTP2_SERVER_DLOG << "Http2ServerTransport ReadLoop Continue";
      return Continue();
    });
  }));
}

auto Http2ServerTransport::OnReadLoopEnded() {
  HTTP2_SERVER_DLOG << "Http2ServerTransport OnReadLoopEnded Factory";
  return [self = RefAsSubclass<Http2ServerTransport>()](absl::Status status) {
    // TODO(tjagtap) : [PH2][P2] : Implement this.
    HTTP2_SERVER_DLOG << "Http2ServerTransport OnReadLoopEnded Promise Status="
                      << status;
    GRPC_UNUSED absl::Status error_status =
        self->HandleError(Http2Status::AbslConnectionError(
            status.code(), std::string(status.message())));
  };
}

auto Http2ServerTransport::WriteFromQueue() {
  HTTP2_SERVER_DLOG << "Http2ServerTransport WriteFromQueue Factory";
  return []() -> Poll<absl::Status> {
    // TODO(tjagtap) : [PH2][P2] : Implement this.
    // Read from the mpsc queue and write it to endpoint
    HTTP2_SERVER_DLOG << "Http2ServerTransport WriteFromQueue Promise";
    return Pending{};
  };
}

auto Http2ServerTransport::WriteLoop() {
  HTTP2_SERVER_DLOG << "Http2ServerTransport WriteLoop Factory";
  return AssertResultType<absl::Status>(Loop([this]() {
    return TrySeq(WriteFromQueue(), []() -> LoopCtl<absl::Status> {
      HTTP2_SERVER_DLOG << "Http2ServerTransport WriteLoop Continue";
      return Continue();
    });
  }));
}

auto Http2ServerTransport::OnWriteLoopEnded() {
  HTTP2_SERVER_DLOG << "Http2ServerTransport OnWriteLoopEnded Factory";
  return [self = RefAsSubclass<Http2ServerTransport>()](absl::Status status) {
    // TODO(tjagtap) : [PH2][P2] : Implement this.
    HTTP2_SERVER_DLOG << "Http2ServerTransport OnWriteLoopEnded Promise Status="
                      << status;
    GRPC_UNUSED absl::Status error_status =
        self->HandleError(Http2Status::AbslConnectionError(
            status.code(), std::string(status.message())));
  };
}

Http2ServerTransport::Http2ServerTransport(
    PromiseEndpoint endpoint, GRPC_UNUSED const ChannelArgs& channel_args,
    std::shared_ptr<EventEngine> event_engine)
    : endpoint_(std::move(endpoint)), outgoing_frames_(kMpscSize) {
  // TODO(tjagtap) : [PH2][P2] : Save and apply channel_args.
  // TODO(tjagtap) : [PH2][P2] : Initialize settings_ to appropriate values.

  HTTP2_SERVER_DLOG << "Http2ServerTransport Constructor Begin";

  // Initialize the general party and write party.
  auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
  general_party_arena->SetContext<EventEngine>(event_engine.get());
  general_party_ = Party::Make(std::move(general_party_arena));

  general_party_->Spawn("ReadLoop", ReadLoop(), OnReadLoopEnded());
  general_party_->Spawn("WriteLoop", WriteLoop(), OnWriteLoopEnded());
  HTTP2_SERVER_DLOG << "Http2ServerTransport Constructor End";
}

Http2ServerTransport::~Http2ServerTransport() {
  // TODO(tjagtap) : [PH2][P2] : Implement the needed cleanup
  HTTP2_SERVER_DLOG << "Http2ServerTransport Destructor Begin";
  general_party_.reset();
  HTTP2_SERVER_DLOG << "Http2ServerTransport Destructor End";
}

}  // namespace http2
}  // namespace grpc_core
