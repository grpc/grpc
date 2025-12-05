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

#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_promises.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/stream.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/log/log.h"

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
// Settings helpers

void InitLocalSettings(Http2Settings& settings, bool is_client);

////////////////////////////////////////////////////////////////////////////////
// Channel Args helpers

struct TransportChannelArgs {
  Duration keepalive_time;
  Duration keepalive_timeout;
  Duration ping_timeout;
  Duration settings_timeout;
  bool keepalive_permit_without_calls;
  bool enable_preferred_rx_crypto_frame_advertisement;
  uint32_t max_header_list_size_soft_limit;
  int max_usable_hpack_table_size;
  int initial_sequence_number;

  std::string DebugString() const {
    return absl::StrCat(
        "keepalive_time: ", keepalive_time,
        " keepalive_timeout: ", keepalive_timeout,
        " ping_timeout: ", ping_timeout,
        " settings_timeout: ", settings_timeout,
        " keepalive_permit_without_calls: ", keepalive_permit_without_calls,
        " enable_preferred_rx_crypto_frame_advertisement: ",
        enable_preferred_rx_crypto_frame_advertisement,
        " max_header_list_size_soft_limit: ", max_header_list_size_soft_limit,
        " max_usable_hpack_table_size: ", max_usable_hpack_table_size,
        " initial_sequence_number: ", initial_sequence_number);
  }
};

void ReadChannelArgs(const ChannelArgs& channel_args,
                     TransportChannelArgs& args, Http2Settings& local_settings,
                     chttp2::TransportFlowControl& flow_control,
                     bool is_client);

void ReadSettingsFromChannelArgs(const ChannelArgs& channel_args,
                                 Http2Settings& local_settings,
                                 chttp2::TransportFlowControl& flow_control,
                                 bool is_client);

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

///////////////////////////////////////////////////////////////////////////////
// Header and Continuation frame processing helpers

// This function is used to partially process a HEADER or CONTINUATION frame.
// `PARTIAL PROCESSING` means reading the payload of a HEADER or CONTINUATION
// and processing it with the HPACK decoder, and then discarding the payload.
// This is done to keep the transports HPACK parser in sync with peers HPACK.
// Scenarios where 'partial processing' is used:
//
// Case 1: Received a HEADER/CONTINUATION frame
// 1. If the frame is invalid ('ParseHeaderFrame'/'ParseContinuationFrame'
//    returns a non-OK status) then it is a connection error. In this case, we
//    do NOT invoke 'partial processing' as the transport is about to be closed
//    anyway.
// 2. If ParseFramePayload returns a non-OK status, then it is a connection
//    error. In this case, we do NOT invoke 'partial processing' as the
//    transport is about to be closed anyway.
// 3. If the frame is valid, but lookup stream fails, then we invoke 'partial
//    processing' and pass the current payload through the HPACK decoder. This
//    can happen if the stream was already closed.
// 4. If the frame is valid, lookup stream succeeds and we fail while processing
//    the frame (be it stream or connection error), we first parse the buffered
//    payload (if any) in the stream through the HPACK decoder and then pass the
//    current payload through the HPACK decoder.
// Case 2: Stream close
// 1. If the stream is being aborted by the upper layers or the transport hit
//    a stream error on a stream while reading HEADER/CONTINUATION frames, we
//    invoke 'partial processing' to parse the enqueued buffer (if any) in the
//    stream to keep our HPACK state consistent with the peer right before
//    closing the stream. This is done as the next time a HEADER/CONTINUATION
//    frame is received from the peer, the stream lookup will start failing.
// This function returns a connection error if HPACK parsing fails. Otherwise,
// it returns the original status.
Http2Status ParseAndDiscardHeaders(HPackParser& parser, SliceBuffer&& buffer,
                                   HeaderAssembler::ParseHeaderArgs args,
                                   RefCountedPtr<Stream> stream,
                                   Http2Status&& original_status);

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_TRANSPORT_H
