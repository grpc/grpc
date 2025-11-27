//
// Copyright 2017 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_MANAGER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_MANAGER_H

#include <grpc/support/port_platform.h>
#include <stdint.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "src/core/channelz/property_list.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// TODO(tjagtap) [PH2][P1][Settings] : Add new DCHECKs to PH2-Only functions in
// this class.
class Http2SettingsManager {
 public:
  // Only local and peer settings can be edited by the transport.
  Http2Settings& mutable_local() { return local_; }
  Http2Settings& mutable_peer() { return peer_; }

  const Http2Settings& local() const { return local_; }
  // Before the first SETTINGS ACK frame is received acked_ will hold the
  // default values.
  const Http2Settings& acked() const { return acked_; }
  const Http2Settings& peer() const { return peer_; }

  channelz::PropertyGrid ChannelzProperties() const {
    return channelz::PropertyGrid()
        .SetColumn("local", local_.ChannelzProperties())
        .SetColumn("sent", sent_.ChannelzProperties())
        .SetColumn("peer", peer_.ChannelzProperties())
        .SetColumn("acked", acked_.ChannelzProperties());
  }

  // Returns std::nullopt if we don't need to send a SETTINGS frame to the peer.
  // Returns Http2SettingsFrame if we need to send a SETTINGS frame to the
  // peer. Transport MUST send a frame returned by this function to the peer.
  // This function is not idempotent.
  std::optional<Http2SettingsFrame> MaybeSendUpdate();

  // Returns 0 if we don't need to send a SETTINGS ACK frame to the peer.
  // Returns n>0 if we need to send n SETTINGS ACK frames to the peer.
  // Transport MUST send one SETTINGS ACK frame for each count returned by this
  // function to the peer.
  // This function is not idempotent.
  uint32_t MaybeSendAck();
  void OnSettingsReceived() { ++num_acks_to_send_; }

  // To be called from a promise based HTTP2 transport only
  http2::Http2ErrorCode ApplyIncomingSettings(
      const std::vector<Http2SettingsFrame::Setting>& settings) {
    for (const auto& setting : settings) {
      http2::Http2ErrorCode error1 =
          count_updates_.IsUpdatePermitted(setting.id, setting.value, peer_);
      if (GPR_UNLIKELY(error1 != http2::Http2ErrorCode::kNoError)) {
        return error1;
      }
      http2::Http2ErrorCode error = peer_.Apply(setting.id, setting.value);
      if (GPR_UNLIKELY(error != http2::Http2ErrorCode::kNoError)) {
        return error;
      }
    }
    return http2::Http2ErrorCode::kNoError;
  }

  // Call when we receive an ACK from our peer.
  // This function is not idempotent.
  GRPC_MUST_USE_RESULT bool AckLastSend();

  GRPC_MUST_USE_RESULT bool IsPreviousSettingsPromiseResolved() const {
    return did_previous_settings_promise_resolve_;
  }
  void SetPreviousSettingsPromiseResolved(const bool value) {
    did_previous_settings_promise_resolve_ = value;
  }

 private:
  struct CountUpdates {
    http2::Http2ErrorCode IsUpdatePermitted(const uint16_t setting_id,
                                            const uint32_t value,
                                            const Http2Settings& peer) {
      switch (setting_id) {
        case Http2Settings::kGrpcAllowTrueBinaryMetadataWireId:
          // These settings must not change more than once. This is a gRPC
          // defined settings.
          if (allow_true_binary_metadata_update &&
              peer.allow_true_binary_metadata() != static_cast<bool>(value)) {
            return http2::Http2ErrorCode::kConnectError;
          }
          allow_true_binary_metadata_update = true;
          break;
        case Http2Settings::kGrpcAllowSecurityFrameWireId:
          // These settings must not change more than once. This is a gRPC
          // defined settings.
          if (allow_security_frame_update &&
              peer.allow_security_frame() != static_cast<bool>(value)) {
            return http2::Http2ErrorCode::kConnectError;
          }
          allow_security_frame_update = true;
          break;
        default:
          break;
      }
      return http2::Http2ErrorCode::kNoError;
    }
    bool allow_true_binary_metadata_update = false;
    bool allow_security_frame_update = false;
  };
  CountUpdates count_updates_;

  enum class UpdateState : uint8_t {
    kFirst,
    kSending,
    kIdle,
  };
  UpdateState update_state_ = UpdateState::kFirst;

  // This holds a copy of the peers settings.
  Http2Settings peer_;

  // These are different sets of our settings.
  // local_  : Setting that has been changed inside our transport,
  //           but not yet sent to the peer.
  // sent_   : New settings frame is sent to the peer but we have not yet
  //           received the ACK from the peer.
  // acked_  : The settings that have already been ACKed by the peer. These
  //           settings can be enforced and any violation of these settings by a
  //           peer may cause an error.
  Http2Settings local_;
  Http2Settings sent_;
  Http2Settings acked_;

  // For CHTTP2, MaybeSendUpdate() checks `update_state_` to ensure only one
  // SETTINGS frame is in flight at a time. PH2 requires an additional
  // constraint: a new SETTINGS frame cannot be sent until the SETTINGS-ACK
  // timeout promise for the previous frame has resolved. This flag tracks this
  // condition for PH2.
  // TODO(tjagtap) [PH2][P1][Settings] : Refactor this.
  bool did_previous_settings_promise_resolve_ = true;

  // Number of incoming SETTINGS frames that we have received but not ACKed yet.
  uint32_t num_acks_to_send_ = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_MANAGER_H
