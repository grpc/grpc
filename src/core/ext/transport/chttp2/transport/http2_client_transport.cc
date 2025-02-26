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

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/match_promise.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace http2 {

#define HTTP2_CLIENT_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

using grpc_event_engine::experimental::EventEngine;

// Experimental : This is just the initial skeleton of class
// and it is functions. The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P3] : Delete this comment when http2
// rollout begins

void Http2ClientTransport::StartCall(GRPC_UNUSED CallHandler call_handler) {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport StartCall Begin";
  // TODO(tjagtap) : [PH2][P1] : Implement this function.
  // TODO(tjagtap) : [PH2][P1] : Add CallHandler to Stream. Add the stream to
  // our stream_list_
  HTTP2_CLIENT_DLOG << "Http2ClientTransport StartCall End";
}

void Http2ClientTransport::PerformOp(GRPC_UNUSED grpc_transport_op* op) {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport PerformOp Begin";
  // TODO(tjagtap) : [PH2][P1] : Implement this function.
  HTTP2_CLIENT_DLOG << "Http2ClientTransport PerformOp End";
}

void Http2ClientTransport::Orphan() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport Orphan Begin";
  // TODO(tjagtap) : [PH2][P1] : Implement the needed cleanup
  Unref();
  HTTP2_CLIENT_DLOG << "Http2ClientTransport Orphan End";
}

void Http2ClientTransport::AbortWithError() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport AbortWithError Begin";
  // TODO(tjagtap) : [PH2][P1] : Implement this function.
  HTTP2_CLIENT_DLOG << "Http2ClientTransport AbortWithError End";
}

auto ProcessHttp2DataFrame(Http2DataFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-data
  HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessHttp2DataFrame Factory";
  return [frame1 = std::move(frame)]() -> absl::Status {
    // TODO(tjagtap) : [PH2][P1] : Implement this.
    HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2DataFrame Promise { stream_id="
        << frame1.stream_id << ", end_stream=" << frame1.end_stream
        << ", payload=" << frame1.payload.JoinIntoString() << "}";
    return absl::OkStatus();
  };
}

auto ProcessHttp2HeaderFrame(Http2HeaderFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-headers
  HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessHttp2HeaderFrame Factory";
  return [frame1 = std::move(frame)]() -> absl::Status {
    // TODO(tjagtap) : [PH2][P1] : Implement this.
    HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2HeaderFrame Promise { stream_id="
        << frame1.stream_id << ", end_headers=" << frame1.end_headers
        << ", end_stream=" << frame1.end_stream
        << ", payload=" << frame1.payload.JoinIntoString() << " }";
    return absl::OkStatus();
  };
}

auto ProcessHttp2RstStreamFrame(Http2RstStreamFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-rst_stream
  HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ProcessHttp2RstStreamFrame Factory";
  return [frame1 = frame]() -> absl::Status {
    // TODO(tjagtap) : [PH2][P1] : Implement this.
    HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2RstStreamFrame Promise{ stream_id="
        << frame1.stream_id << ", error_code=" << frame1.error_code << " }";
    return absl::OkStatus();
  };
}

auto ProcessHttp2SettingsFrame(Http2SettingsFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-settings
  HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessHttp2SettingsFrame Factory";
  return [frame1 = std::move(frame)]() -> absl::Status {
    // TODO(tjagtap) : [PH2][P1] : Implement this.
    // Load into this.settings_
    // Take necessary actions as per settings that have changed.
    HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2SettingsFrame Promise { ack="
        << frame1.ack << ", settings length=" << frame1.settings.size() << "}";
    return absl::OkStatus();
  };
}

auto ProcessHttp2PingFrame(Http2PingFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-ping
  HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessHttp2PingFrame Factory";
  return [frame1 = frame]() -> absl::Status {
    // TODO(tjagtap) : [PH2][P1] : Implement this.
    HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2PingFrame Promise { ack="
        << frame1.ack << ", opaque=" << frame1.opaque << " }";
    return absl::OkStatus();
  };
}

auto ProcessHttp2GoawayFrame(Http2GoawayFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-goaway
  HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessHttp2GoawayFrame Factory";
  return [frame1 = std::move(frame)]() -> absl::Status {
    // TODO(tjagtap) : [PH2][P1] : Implement this.
    HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2GoawayFrame Promise { "
           "last_stream_id="
        << frame1.last_stream_id << ", error_code=" << frame1.error_code
        << ", debug_data=" << frame1.debug_data.as_string_view() << "}";
    return absl::OkStatus();
  };
}

auto ProcessHttp2WindowUpdateFrame(Http2WindowUpdateFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-window_update
  HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ProcessHttp2WindowUpdateFrame Factory";
  return [frame1 = frame]() -> absl::Status {
    // TODO(tjagtap) : [PH2][P1] : Implement this.
    HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2WindowUpdateFrame Promise { "
           " stream_id="
        << frame1.stream_id << ", increment=" << frame1.increment << "}";
    return absl::OkStatus();
  };
}

auto ProcessHttp2ContinuationFrame(Http2ContinuationFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-continuation
  HTTP2_CLIENT_DLOG
      << "Http2ClientTransport ProcessHttp2ContinuationFrame Factory";
  return [frame1 = std::move(frame)]() -> absl::Status {
    // TODO(tjagtap) : [PH2][P1] : Implement this.
    HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2ContinuationFrame Promise { "
           "stream_id="
        << frame1.stream_id << ", end_headers=" << frame1.end_headers
        << ", payload=" << frame1.payload.JoinIntoString() << " }";
    return absl::OkStatus();
  };
}

auto ProcessHttp2SecurityFrame(Http2SecurityFrame frame) {
  // TODO(tjagtap) : [PH2][P2] : This is not in the RFC. Understand usage.
  HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessHttp2SecurityFrame Factory";
  return [frame1 = std::move(frame)]() -> absl::Status {
    // TODO(tjagtap) : [PH2][P2] : Implement this.
    HTTP2_CLIENT_DLOG
        << "Http2ClientTransport ProcessHttp2SecurityFrame Promise { payload="
        << frame1.payload.JoinIntoString() << " }";
    return absl::OkStatus();
  };
}

auto Http2ClientTransport::ProcessOneFrame(Http2Frame frame) {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport ProcessOneFrame Factory";
  return AssertResultType<absl::Status>(MatchPromise(
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
        return absl::OkStatus();
      }));
}

auto Http2ClientTransport::ReadAndProcessOneFrame() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport ReadAndProcessOneFrame Factory";
  return AssertResultType<absl::Status>(TrySeq(
      // Fetch the first kFrameHeaderSize bytes of the Frame, these contain
      // the frame header.
      endpoint_.ReadSlice(kFrameHeaderSize),
      // Parse the frame header.
      [](Slice header_bytes) -> Http2FrameHeader {
        HTTP2_CLIENT_DLOG
            << "Http2ClientTransport ReadAndProcessOneFrame Parse "
            << header_bytes.as_string_view();
        return Http2FrameHeader::Parse(header_bytes.begin());
      },
      // Read the payload of the frame.
      [this](Http2FrameHeader header) {
        // TODO(tjagtap) : [PH2][P3] : This is not nice. Fix by using Stapler.
        HTTP2_CLIENT_DLOG << "Http2ClientTransport ReadAndProcessOneFrame Read";
        current_frame_header_ = header;
        return AssertResultType<absl::StatusOr<SliceBuffer>>(
            endpoint_.Read(header.length));
      },
      // Parse the payload of the frame based on frame type.
      [this](SliceBuffer payload) -> absl::StatusOr<Http2Frame> {
        HTTP2_CLIENT_DLOG
            << "Http2ClientTransport ReadAndProcessOneFrame ParseFramePayload "
            << payload.JoinIntoString();
        return ParseFramePayload(current_frame_header_, std::move(payload));
      },
      [this](GRPC_UNUSED Http2Frame frame) {
        HTTP2_CLIENT_DLOG
            << "Http2ClientTransport ReadAndProcessOneFrame ProcessOneFrame";
        return AssertResultType<absl::Status>(
            ProcessOneFrame(std::move(frame)));
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
  return [](absl::Status status) -> absl::Status {
    // TODO(tjagtap) : [PH2][P1] : Implement this.
    HTTP2_CLIENT_DLOG << "Http2ClientTransport OnReadLoopEnded Promise Status="
                      << status;
    return status;
  };
}

auto Http2ClientTransport::WriteFromQueue() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport WriteFromQueue Factory";
  return []() -> Poll<absl::Status> {
    // TODO(tjagtap) : [PH2][P1] : Implement this.
    // Read from the mpsc queue and write it to endpoint
    HTTP2_CLIENT_DLOG << "Http2ClientTransport WriteFromQueue Promise";
    return Pending{};
  };
}

auto Http2ClientTransport::WriteLoop() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport WriteLoop Factory";
  return AssertResultType<absl::Status>(Loop([this]() {
    return TrySeq(WriteFromQueue(), []() -> LoopCtl<absl::Status> {
      HTTP2_CLIENT_DLOG << "Http2ClientTransport WriteLoop Continue";
      return Continue();
    });
  }));
}

auto Http2ClientTransport::OnWriteLoopEnded() {
  HTTP2_CLIENT_DLOG << "Http2ClientTransport OnWriteLoopEnded Factory";
  return [](absl::Status status) {
    // TODO(tjagtap) : [PH2][P1] : Implement this.
    HTTP2_CLIENT_DLOG << "Http2ClientTransport OnWriteLoopEnded Promise Status="
                      << status;
    return status;
  };
}

Http2ClientTransport::Http2ClientTransport(
    PromiseEndpoint endpoint, GRPC_UNUSED const ChannelArgs& channel_args,
    std::shared_ptr<EventEngine> event_engine)
    : endpoint_(std::move(endpoint)) {
  // TODO(tjagtap) : [PH2][P1] : Save and apply channel_args.
  // TODO(tjagtap) : [PH2][P1] : Initialize settings_ to appropriate values.

  HTTP2_CLIENT_DLOG << "Http2ClientTransport Constructor Begin";

  // Initialize the general party and write party.
  auto general_party_arena = SimpleArenaAllocator(0)->MakeArena();
  general_party_arena->SetContext<EventEngine>(event_engine.get());
  general_party_ = Party::Make(std::move(general_party_arena));

  auto write_party_arena = SimpleArenaAllocator(0)->MakeArena();
  write_party_arena->SetContext<EventEngine>(event_engine.get());
  write_party_ = Party::Make(std::move(write_party_arena));

  general_party_->Spawn("ReadLoop", ReadLoop(), OnReadLoopEnded());
  write_party_->Spawn("WriteLoop", WriteLoop(), OnWriteLoopEnded());
  HTTP2_CLIENT_DLOG << "Http2ClientTransport Constructor End";
}

Http2ClientTransport::~Http2ClientTransport() {
  // TODO(tjagtap) : [PH2][P1] : Implement the needed cleanup
  HTTP2_CLIENT_DLOG << "Http2ClientTransport Destructor Begin";
  general_party_.reset();
  write_party_.reset();
  HTTP2_CLIENT_DLOG << "Http2ClientTransport Destructor End";
}

}  // namespace http2
}  // namespace grpc_core
