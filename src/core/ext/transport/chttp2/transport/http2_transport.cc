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

#include "src/core/ext/transport/chttp2/transport/http2_transport.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/metadata_info.h"
#include "src/core/channelz/channelz.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/header_assembler.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_promises.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/internal_channel_arg_names.h"
#include "src/core/ext/transport/chttp2/transport/stream.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "absl/log/log.h"
#include "absl/types/span.h"

namespace grpc_core {
namespace http2 {

constexpr Duration kDefaultPingTimeout = Duration::Minutes(1);
constexpr Duration kDefaultKeepaliveTimeout = Duration::Seconds(20);
constexpr bool kDefaultKeepalivePermitWithoutCalls = false;
constexpr bool kDefaultEnablePreferredRxCryptoFrameAdvertisement = false;

constexpr Duration kClientKeepaliveTime = Duration::Infinity();

constexpr Duration kServerKeepaliveTime = Duration::Hours(2);

// Experimental : This is just the initial skeleton of class
// and it is functions. The code will be written iteratively.
// Do not use or edit any of these functions unless you are
// familiar with the PH2 project (Moving chttp2 to promises.)

///////////////////////////////////////////////////////////////////////////////
// Settings helpers

void InitLocalSettings(Http2Settings& settings, const bool is_client) {
  if (is_client) {
    // gRPC has never supported PUSH_PROMISE and we have no plan to do so in the
    // future.
    settings.SetEnablePush(false);
    // This is to make it double-sure that server cannot initiate a stream.
    settings.SetMaxConcurrentStreams(0);
  }
  settings.SetMaxHeaderListSize(DEFAULT_MAX_HEADER_LIST_SIZE);
  settings.SetAllowTrueBinaryMetadata(true);
}

////////////////////////////////////////////////////////////////////////////////
// Channel Args helpers
void ReadChannelArgs(const ChannelArgs& channel_args,
                     TransportChannelArgs& args, Http2Settings& local_settings,
                     chttp2::TransportFlowControl& flow_control,
                     bool is_client) {
  ReadSettingsFromChannelArgs(channel_args, local_settings, flow_control,
                              is_client);

  args.max_header_list_size_soft_limit =
      GetSoftLimitFromChannelArgs(channel_args);
  args.keepalive_time = std::max(
      Duration::Milliseconds(1),
      channel_args.GetDurationFromIntMillis(GRPC_ARG_KEEPALIVE_TIME_MS)
          .value_or(is_client ? kClientKeepaliveTime : kServerKeepaliveTime));
  args.keepalive_timeout = std::max(
      Duration::Zero(),
      channel_args.GetDurationFromIntMillis(GRPC_ARG_KEEPALIVE_TIMEOUT_MS)
          .value_or(args.keepalive_time == Duration::Infinity()
                        ? Duration::Infinity()
                        : kDefaultKeepaliveTimeout));
  args.ping_timeout =
      std::max(Duration::Zero(),
               channel_args.GetDurationFromIntMillis(GRPC_ARG_PING_TIMEOUT_MS)
                   .value_or(args.keepalive_time == Duration::Infinity()
                                 ? Duration::Infinity()
                                 : kDefaultPingTimeout));
  args.settings_timeout =
      channel_args.GetDurationFromIntMillis(GRPC_ARG_SETTINGS_TIMEOUT)
          .value_or(std::max(args.keepalive_timeout * 2, Duration::Minutes(1)));

  args.keepalive_permit_without_calls =
      channel_args.GetBool(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS)
          .value_or(kDefaultKeepalivePermitWithoutCalls);

  args.enable_preferred_rx_crypto_frame_advertisement =
      channel_args
          .GetBool(GRPC_ARG_EXPERIMENTAL_HTTP2_PREFERRED_CRYPTO_FRAME_SIZE)
          .value_or(kDefaultEnablePreferredRxCryptoFrameAdvertisement);

  args.max_usable_hpack_table_size =
      channel_args.GetInt(GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER).value_or(-1);

  args.initial_sequence_number =
      channel_args.GetInt(GRPC_ARG_HTTP2_INITIAL_SEQUENCE_NUMBER).value_or(-1);
  if (args.initial_sequence_number >= 0 &&
      (args.initial_sequence_number & 1) == 0) {
    LOG(ERROR) << "Initial sequence number MUST be odd. Ignoring the value.";
    args.initial_sequence_number = -1;
  }

  GRPC_HTTP2_COMMON_DLOG << "ChannelArgs: " << args.DebugString();
}

void ReadSettingsFromChannelArgs(const ChannelArgs& channel_args,
                                 Http2Settings& local_settings,
                                 chttp2::TransportFlowControl& flow_control,
                                 const bool is_client) {
  if (channel_args.Contains(GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER)) {
    local_settings.SetHeaderTableSize(
        channel_args.GetInt(GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER)
            .value_or(-1));
  }

  if (channel_args.Contains(GRPC_ARG_MAX_CONCURRENT_STREAMS)) {
    if (!is_client) {
      local_settings.SetMaxConcurrentStreams(
          channel_args.GetInt(GRPC_ARG_MAX_CONCURRENT_STREAMS).value_or(-1));
    } else {
      // We do not allow the channel arg to alter our 0 setting for
      // MAX_CONCURRENT_STREAMS for clients because we don't support
      // PUSH_PROMISE
      LOG(WARNING) << "ChannelArg GRPC_ARG_MAX_CONCURRENT_STREAMS is not "
                      "available on clients";
    }
  }

  if (channel_args.Contains(GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES)) {
    int value =
        channel_args.GetInt(GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES).value_or(-1);
    if (value >= 0) {
      local_settings.SetInitialWindowSize(value);
      flow_control.set_target_initial_window_size(value);
    }
  }

  local_settings.SetMaxHeaderListSize(
      GetHardLimitFromChannelArgs(channel_args));

  if (channel_args.Contains(GRPC_ARG_HTTP2_MAX_FRAME_SIZE)) {
    local_settings.SetMaxFrameSize(
        channel_args.GetInt(GRPC_ARG_HTTP2_MAX_FRAME_SIZE).value_or(-1));
  }

  if (channel_args
          .GetBool(GRPC_ARG_EXPERIMENTAL_HTTP2_PREFERRED_CRYPTO_FRAME_SIZE)
          .value_or(false)) {
    local_settings.SetPreferredReceiveCryptoMessageSize(INT_MAX);
  }

  if (channel_args.Contains(GRPC_ARG_HTTP2_ENABLE_TRUE_BINARY)) {
    local_settings.SetAllowTrueBinaryMetadata(
        channel_args.GetInt(GRPC_ARG_HTTP2_ENABLE_TRUE_BINARY).value_or(-1) !=
        0);
  }

  local_settings.SetAllowSecurityFrame(
      channel_args.GetBool(GRPC_ARG_SECURITY_FRAME_ALLOWED).value_or(false));

  // TODO(tjagtap) : [PH2][P4] : If max_header_list_size is set only once
  // in the life of a transport, consider making this a data member of
  // class IncomingMetadataTracker instead of accessing via acked settings again
  // and again. Else delete this comment.

  GRPC_HTTP2_COMMON_DLOG
      << "Http2Settings: {"
      << "header_table_size: " << local_settings.header_table_size()
      << ", max_concurrent_streams: " << local_settings.max_concurrent_streams()
      << ", initial_window_size: " << local_settings.initial_window_size()
      << ", max_frame_size: " << local_settings.max_frame_size()
      << ", max_header_list_size: " << local_settings.max_header_list_size()
      << ", preferred_receive_crypto_message_size: "
      << local_settings.preferred_receive_crypto_message_size()
      << ", enable_push: " << local_settings.enable_push()
      << ", allow_true_binary_metadata: "
      << local_settings.allow_true_binary_metadata()
      << ", allow_security_frame: " << local_settings.allow_security_frame()
      << "}";
}

///////////////////////////////////////////////////////////////////////////////
// ChannelZ helpers

RefCountedPtr<channelz::SocketNode> CreateChannelzSocketNode(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        event_engine_endpoint,
    const ChannelArgs& args) {
  if (args.GetBool(GRPC_ARG_ENABLE_CHANNELZ)
          .value_or(GRPC_ENABLE_CHANNELZ_DEFAULT)) {
    auto local_addr = grpc_event_engine::experimental::ResolvedAddressToString(
        event_engine_endpoint->GetLocalAddress());
    auto peer_addr = grpc_event_engine::experimental::ResolvedAddressToString(
        event_engine_endpoint->GetPeerAddress());
    GRPC_HTTP2_COMMON_DLOG << "CreateChannelzSocketNode: local_addr: "
                           << local_addr.value_or("unknown")
                           << " peer_addr: " << peer_addr.value_or("unknown");
    return MakeRefCounted<channelz::SocketNode>(
        local_addr.value_or("unknown"), peer_addr.value_or("unknown"),
        absl::StrCat("http2", " ", peer_addr.value_or("unknown")),
        args.GetObjectRef<channelz::SocketNode::Security>());
  }
  return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// Flow control helpers

void ProcessOutgoingDataFrameFlowControl(
    chttp2::StreamFlowControl& stream_flow_control,
    const uint32_t flow_control_tokens_consumed) {
  if (flow_control_tokens_consumed > 0) {
    chttp2::StreamFlowControl::OutgoingUpdateContext fc_update(
        &stream_flow_control);
    // This updates flow control tokens for both stream and transport flow
    // control.
    fc_update.SentData(flow_control_tokens_consumed);
  }
}

ValueOrHttp2Status<chttp2::FlowControlAction>
ProcessIncomingDataFrameFlowControl(Http2FrameHeader& frame_header,
                                    chttp2::TransportFlowControl& flow_control,
                                    RefCountedPtr<Stream> stream) {
  GRPC_DCHECK_EQ(frame_header.type, 0u);
  if (frame_header.length > 0) {
    if (stream == nullptr) {
      // This flow control bookkeeping needs to happen even though the stream is
      // gone because otherwise we will go out-of-sync with the peer.
      // The flow control numbers should be consistent for both peers.
      chttp2::TransportFlowControl::IncomingUpdateContext transport_fc(
          &flow_control);
      absl::Status fc_status = transport_fc.RecvData(frame_header.length);
      chttp2::FlowControlAction action = transport_fc.MakeAction();
      if (!fc_status.ok()) {
        LOG(ERROR) << "Flow control error: " << fc_status.message();
        // RFC9113 : A receiver MAY respond with a stream error or connection
        // error of type FLOW_CONTROL_ERROR if it is unable to accept a frame.
        return Http2Status::Http2ConnectionError(
            Http2ErrorCode::kFlowControlError,
            std::string(fc_status.message()));
      }
      return action;
    } else {
      chttp2::StreamFlowControl::IncomingUpdateContext stream_fc(
          &stream->flow_control);
      absl::Status fc_status = stream_fc.RecvData(frame_header.length);
      chttp2::FlowControlAction action = stream_fc.MakeAction();
      if (!fc_status.ok()) {
        LOG(ERROR) << "Flow control error: " << fc_status.message();
        // RFC9113 : A receiver MAY respond with a stream error or connection
        // error of type FLOW_CONTROL_ERROR if it is unable to accept a frame.
        return Http2Status::Http2ConnectionError(
            Http2ErrorCode::kFlowControlError,
            std::string(fc_status.message()));
      }
      return action;
    }
  }
  return chttp2::FlowControlAction();
}

bool ProcessIncomingWindowUpdateFrameFlowControl(
    const Http2WindowUpdateFrame& frame,
    chttp2::TransportFlowControl& flow_control, RefCountedPtr<Stream> stream) {
  if (frame.stream_id != 0) {
    if (stream != nullptr) {
      chttp2::StreamFlowControl::OutgoingUpdateContext fc_update(
          &stream->flow_control);
      fc_update.RecvUpdate(frame.increment);
    } else {
      // If stream id is non zero, and stream is nullptr, maybe the stream was
      // closed. Ignore this WINDOW_UPDATE frame.
    }
  } else {
    chttp2::TransportFlowControl::OutgoingUpdateContext fc_update(
        &flow_control);
    fc_update.RecvUpdate(frame.increment);
    if (fc_update.Finish() == chttp2::StallEdge::kUnstalled) {
      // If transport moves from kStalled to kUnstalled, streams blocked by
      // transport flow control will become writable. Return true to trigger a
      // write cycle and attempt to send data from these streams.
      // Although it's possible no streams were blocked, triggering an
      // unnecessary write cycle in that super-rare case is acceptable.
      return true;
    }
  }
  return false;
}

// /////////////////////////////////////////////////////////////////////////////
// Header and Continuation frame processing helpers
Http2Status ParseAndDiscardHeaders(HPackParser& parser, SliceBuffer&& buffer,
                                   HeaderAssembler::ParseHeaderArgs args,
                                   const RefCountedPtr<Stream> stream,
                                   Http2Status&& original_status) {
  GRPC_HTTP2_COMMON_DLOG << "ParseAndDiscardHeaders buffer "
                            "size: "
                         << buffer.Length() << " args: " << args.DebugString()
                         << " stream_id: "
                         << (stream == nullptr ? 0 : stream->GetStreamId())
                         << " original_status: "
                         << original_status.DebugString();

  if (stream != nullptr) {
    // Parse all the data in the header assembler
    Http2Status result = stream->header_assembler.ParseAndDiscardHeaders(
        parser, args.is_initial_metadata, args.is_client,
        args.max_header_list_size_soft_limit,
        args.max_header_list_size_hard_limit);
    if (!result.IsOk()) {
      GRPC_DCHECK(result.GetType() ==
                  Http2Status::Http2ErrorType::kConnectionError);
      LOG(ERROR) << "Connection Error: " << result;
      return result;
    }
  }

  if (buffer.Length() == 0) {
    return std::move(original_status);
  }

  Http2Status status = HeaderAssembler::ParseHeader(
      parser, std::move(buffer), /*grpc_metadata_batch=*/nullptr, args);

  return (status.IsOk()) ? std::move(original_status) : std::move(status);
}

}  // namespace http2
}  // namespace grpc_core
