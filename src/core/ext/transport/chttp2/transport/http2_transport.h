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
#include <utility>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"

namespace grpc_core {
namespace http2 {

// Experimental : This is just the initial skeleton of class
// and it is functions. The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)
// TODO(tjagtap) : [PH2][P3] : Update the experimental status of the code before
// http2 rollout begins.

#define HTTP2_TRANSPORT_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

#define HTTP2_CLIENT_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

#define HTTP2_SERVER_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

// TODO(akshitpatel) : [PH2][P2] : Choose appropriate size later.
constexpr int kMpscSize = 10;

enum class HttpStreamState {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-stream-states
  kIdle,
  kOpen,
  kHalfClosedLocal,
  kHalfClosedRemote,
  kClosed,
};

// Empty Frame. Can be used in the place of HTTP2 frame type to trigger certain events when needed.
struct EmptyFrameForOperationTrigger {
};

using QueueableFrame =
    std::variant<Http2DataFrame, Http2HeaderFrame, Http2ContinuationFrame,
                 Http2RstStreamFrame, 
                 Http2GoawayFrame, Http2SecurityFrame,
                 EmptyFrameForOperationTrigger>;

class TransportSendQeueue {

};

inline auto ProcessHttp2DataFrame(Http2DataFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-data
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2DataFrame Factory";
  return
      [frame1 = std::move(frame)]() -> absl::Status {
        // TODO(tjagtap) : [PH2][P1] : Implement this.
        HTTP2_TRANSPORT_DLOG
            << "Http2Transport ProcessHttp2DataFrame Promise { stream_id="
            << frame1.stream_id << ", end_stream=" << frame1.end_stream
            << ", payload=" << frame1.payload.JoinIntoString() << "}";
        return absl::OkStatus();
      };
}

inline auto ProcessHttp2HeaderFrame(Http2HeaderFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-headers
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2HeaderFrame Factory";
  return
      [frame1 = std::move(frame)]() -> absl::Status {
        // TODO(tjagtap) : [PH2][P1] : Implement this.
        HTTP2_TRANSPORT_DLOG
            << "Http2Transport ProcessHttp2HeaderFrame Promise { stream_id="
            << frame1.stream_id << ", end_headers=" << frame1.end_headers
            << ", end_stream=" << frame1.end_stream
            << ", payload=" << frame1.payload.JoinIntoString() << " }";
        return absl::OkStatus();
      };
}

inline auto ProcessHttp2RstStreamFrame(Http2RstStreamFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-rst_stream
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2RstStreamFrame Factory";
  return
      [frame1 = frame]() -> absl::Status {
        // TODO(tjagtap) : [PH2][P1] : Implement this.
        HTTP2_TRANSPORT_DLOG
            << "Http2Transport ProcessHttp2RstStreamFrame Promise{ stream_id="
            << frame1.stream_id << ", error_code=" << frame1.error_code << " }";
        return absl::OkStatus();
      };
}

inline auto ProcessHttp2SettingsFrame(Http2SettingsFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-settings
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2SettingsFrame Factory";
  return
      [frame1 = std::move(frame)]() -> absl::Status {
        // TODO(tjagtap) : [PH2][P1] : Implement this.
        // Load into this.settings_
        // Take necessary actions as per settings that have changed.
        HTTP2_TRANSPORT_DLOG
            << "Http2Transport ProcessHttp2SettingsFrame Promise { ack="
            << frame1.ack << ", settings length=" << frame1.settings.size()
            << "}";
        return absl::OkStatus();
      };
}

inline auto ProcessHttp2PingFrame(Http2PingFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-ping
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2PingFrame Factory";
  return
      [frame1 = frame]() -> absl::Status {
        // TODO(tjagtap) : [PH2][P1] : Implement this.
        HTTP2_TRANSPORT_DLOG
            << "Http2Transport ProcessHttp2PingFrame Promise { ack="
            << frame1.ack << ", opaque=" << frame1.opaque << " }";
        return absl::OkStatus();
      };
}

inline auto ProcessHttp2GoawayFrame(Http2GoawayFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-goaway
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2GoawayFrame Factory";
  return
      [frame1 = std::move(frame)]() -> absl::Status {
        // TODO(tjagtap) : [PH2][P1] : Implement this.
        HTTP2_TRANSPORT_DLOG
            << "Http2Transport ProcessHttp2GoawayFrame Promise { "
               "last_stream_id="
            << frame1.last_stream_id << ", error_code=" << frame1.error_code
            << ", debug_data=" << frame1.debug_data.as_string_view() << "}";
        return absl::OkStatus();
      };
}

inline auto ProcessHttp2WindowUpdateFrame(Http2WindowUpdateFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-window_update
  HTTP2_TRANSPORT_DLOG
      << "Http2Transport ProcessHttp2WindowUpdateFrame Factory";
  return
      [frame1 = frame]() -> absl::Status {
        // TODO(tjagtap) : [PH2][P1] : Implement this.
        HTTP2_TRANSPORT_DLOG
            << "Http2Transport ProcessHttp2WindowUpdateFrame Promise { "
               " stream_id="
            << frame1.stream_id << ", increment=" << frame1.increment << "}";
        return absl::OkStatus();
      };
}

inline auto ProcessHttp2ContinuationFrame(Http2ContinuationFrame frame) {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-continuation
  HTTP2_TRANSPORT_DLOG
      << "Http2Transport ProcessHttp2ContinuationFrame Factory";
  return
      [frame1 = std::move(frame)]() -> absl::Status {
        // TODO(tjagtap) : [PH2][P1] : Implement this.
        HTTP2_TRANSPORT_DLOG
            << "Http2Transport ProcessHttp2ContinuationFrame Promise { "
               "stream_id="
            << frame1.stream_id << ", end_headers=" << frame1.end_headers
            << ", payload=" << frame1.payload.JoinIntoString() << " }";
        return absl::OkStatus();
      };
}

inline auto ProcessHttp2SecurityFrame(Http2SecurityFrame frame) {
  // TODO(tjagtap) : [PH2][P2] : This is not in the RFC. Understand usage.
  HTTP2_TRANSPORT_DLOG << "Http2Transport ProcessHttp2SecurityFrame Factory";
  return
      [frame1 = std::move(frame)]() -> absl::Status {
        // TODO(tjagtap) : [PH2][P2] : Implement this.
        HTTP2_TRANSPORT_DLOG
            << "Http2Transport ProcessHttp2SecurityFrame Promise { payload="
            << frame1.payload.JoinIntoString() << " }";
        return absl::OkStatus();
      };
}

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_TRANSPORT_H
