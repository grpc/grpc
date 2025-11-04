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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_MANAGER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_MANAGER_H

#include <algorithm>
#include <cstdint>
#include <vector>

#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "absl/container/flat_hash_map.h"

namespace grpc_core {
namespace http2 {

constexpr chttp2::FlowControlAction::Urgency kNoActionNeeded =
    chttp2::FlowControlAction::Urgency::NO_ACTION_NEEDED;
constexpr chttp2::FlowControlAction::Urgency kUpdateImmediately =
    chttp2::FlowControlAction::Urgency::UPDATE_IMMEDIATELY;

#define GRPC_HTTP2_FLOW_CONTROL_HELPERS \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

// Function to update local settings based on FlowControlAction.
// This function does the settings related tasks equivalent to
// grpc_chttp2_act_on_flowctl_action in chttp2_transport.cc
inline void ActOnFlowControlActionSettings(
    const chttp2::FlowControlAction& action, Http2Settings& local_settings,
    const bool enable_preferred_rx_crypto_frame_advertisement) {
  if (action.send_initial_window_update() != kNoActionNeeded) {
    local_settings.SetInitialWindowSize(action.initial_window_size());
  }
  if (action.send_max_frame_size_update() != kNoActionNeeded) {
    local_settings.SetMaxFrameSize(action.max_frame_size());
  }
  if (enable_preferred_rx_crypto_frame_advertisement &&
      action.preferred_rx_crypto_frame_size_update() != kNoActionNeeded) {
    local_settings.SetPreferredReceiveCryptoMessageSize(
        action.preferred_rx_crypto_frame_size());
  }
}

inline uint32_t GetMaxPermittedDequeue(
    chttp2::TransportFlowControl& transport_flow_control,
    chttp2::StreamFlowControl& stream_flow_control, const size_t upper_limit,
    const Http2Settings& peer_settings) {
  const int64_t flow_control_tokens =
      std::min(transport_flow_control.remote_window(),
               stream_flow_control.remote_window_delta() +
                   peer_settings.initial_window_size());
  uint32_t max_dequeue = 0;
  if (flow_control_tokens > 0) {
    max_dequeue = static_cast<uint32_t>(
        std::min({static_cast<size_t>(flow_control_tokens), upper_limit,
                  static_cast<size_t>(RFC9113::kMaxSize31Bit - 1)}));
  }
  GRPC_HTTP2_FLOW_CONTROL_HELPERS
      << "GetFlowControlTokens flow_control_tokens = " << flow_control_tokens
      << " upper_limit = " << upper_limit << " max_dequeue = " << max_dequeue;
  return max_dequeue;
}

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_MANAGER_H
