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

#include <cstdint>
#include <utility>

#include "src/core/call/call_spine.h"
#include "src/core/call/metadata_info.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
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

void InitLocalSettings(Http2Settings& settings, const bool is_client) {
  if (is_client) {
    // gRPC has never supported PUSH_PROMISE and we have no plan to do so in the
    // future.
    settings.SetEnablePush(false);
    // This is to make it double-sure that server cannot initite a stream.
    settings.SetMaxConcurrentStreams(0);
  }
  settings.SetMaxHeaderListSize(DEFAULT_MAX_HEADER_LIST_SIZE);
  settings.SetAllowTrueBinaryMetadata(true);
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

}  // namespace http2
}  // namespace grpc_core
