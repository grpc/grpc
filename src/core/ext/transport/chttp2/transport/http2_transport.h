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

#include "src/core/call/call_spine.h"
#include "src/core/call/metadata_info.h"
#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/stream.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
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

#define GRPC_HTTP2_CLIENT_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

#define GRPC_HTTP2_CLIENT_ERROR_DLOG \
  LOG_IF(ERROR, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

#define GRPC_HTTP2_COMMON_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

constexpr uint32_t kMaxWriteSize = /*10 MB*/ 10u * 1024u * 1024u;

constexpr uint32_t kGoawaySendTimeoutSeconds = 5u;

///////////////////////////////////////////////////////////////////////////////
// Settings and ChannelArgs helpers

void InitLocalSettings(Http2Settings& settings, const bool is_client);

void ReadSettingsFromChannelArgs(const ChannelArgs& channel_args,
                                 Http2Settings& local_settings,
                                 chttp2::TransportFlowControl& flow_control,
                                 const bool is_client);

///////////////////////////////////////////////////////////////////////////////
// ChannelZ helpers

RefCountedPtr<channelz::SocketNode> CreateChannelzSocketNode(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        event_engine_endpoint,
    const ChannelArgs& args);

///////////////////////////////////////////////////////////////////////////////
// Flow control helpers

void ProcessOutgoingDataFrameFlowControl(
    chttp2::StreamFlowControl& stream_flow_control,
    uint32_t flow_control_tokens_consumed);

ValueOrHttp2Status<chttp2::FlowControlAction>
ProcessIncomingDataFrameFlowControl(Http2FrameHeader& frame,
                                    chttp2::TransportFlowControl& flow_control,
                                    RefCountedPtr<Stream> stream);

// Returns true if a write should be triggered
bool ProcessIncomingWindowUpdateFrameFlowControl(
    const Http2WindowUpdateFrame& frame,
    chttp2::TransportFlowControl& flow_control, RefCountedPtr<Stream> stream);

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_TRANSPORT_H
