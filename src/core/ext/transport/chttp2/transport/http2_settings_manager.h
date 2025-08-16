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
#include <queue>

#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "src/core/channelz/property_list.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/time.h"
#include "src/core/util/useful.h"

namespace grpc_core {

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

  // Returns nullopt if we don't need to send a SETTINGS frame to the peer.
  // Returns Http2SettingsFrame if we need to send a SETTINGS frame to the
  // peer. Transport MUST send a frame returned by this function to the peer.
  // This function is not idempotent.
  std::optional<Http2SettingsFrame> MaybeSendUpdate();

  // To be called from a promise based HTTP2 transport only
  http2::Http2ErrorCode ApplyIncomingSettings(
      std::vector<Http2SettingsFrame::Setting>& settings) {
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

  bool did_previous_settings_promise_resolve_ = true;
};

// Timeout for getting an ack back on settings changes
#define GRPC_ARG_SETTINGS_TIMEOUT "grpc.http2.settings_timeout"

#define GRPC_SETTINGS_TIMEOUT_DLOG DLOG(INFO)

// This class can only be used only from a promise based HTTP2 transports
// general_party_ .
// This class is designed with the assumption that only 1 SETTINGS frame will be
// in flight at a time. And we do not send a second SETTINGS frame till we
// receive and process the SETTINGS ACK.
class SettingsTimeoutManager {
 public:
  // Assumption : This would be set only once in the life of the transport.
  inline void SetSettingsTimeout(const ChannelArgs& channel_args,
                                 const Duration keepalive_timeout) {
    timeout_ =
        channel_args.GetDurationFromIntMillis(GRPC_ARG_SETTINGS_TIMEOUT)
            .value_or(std::max(keepalive_timeout * 2, Duration::Minutes(1)));
  }

  // To be called when a promise based Transport receives an a SETTINGS ACK
  // frame.
  inline void OnSettingsAckReceived() { RecordReceivedAck(); }

  // This returns a promise which must be spawned on transports general party.
  // This must be spawned soon after the transport sends a SETTINGS frame on the
  // endpoint.
  // If we don't get an ACK before timeout, the caller MUST close the transport.
  auto WaitForSettingsTimeout() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsTimeoutManager::WaitForSettingsTimeout Factory";
    StartSettingsTimeoutTimer();
    // TODO(tjagtap) : [PH2][P1] : Make this a ref counted class and manage the
    // lifetime
    return AssertResultType<absl::Status>(
        Race(
            [this]() -> Poll<absl::Status> {
              GRPC_SETTINGS_TIMEOUT_DLOG
                  << "SettingsTimeoutManager::WaitForSettingsTimeout Race";
              // This Promise will "win" the race if we receive the SETTINGS
              // ACK from the peer within the timeout time.
              if (DidReceiveAck()) {
                DCHECK(
                sent_time_ +
                    (timeout_ *
                     1.1 /* 10% grace time for this promise to be scheduled*/) >
                Timestamp::Now())
                << "Should have timed out";
                RemoveReceivedAck();
                return absl::OkStatus();
              }
              AddWaitingForAck();
              return Pending{};
            },
            // This promise will "Win" the Race if timeout is crossed and we did
            // not receive the ACK. The transport must close when this happens.
            TrySeq(Sleep(timeout_), [sent_time = sent_time_,
                                     timeout = timeout_]() {
              GRPC_SETTINGS_TIMEOUT_DLOG
                  << "SettingsTimeoutManager::WaitForSettingsTimeout Timeout"
                     " triggered. Transport will close. Sent Time : "
                  << sent_time << " Timeout Time : " << (sent_time + timeout)
                  << " Current Time " << Timestamp::Now();
              return absl::CancelledError(
                  std::string(RFC9113::kSettingsTimeout));
            })));
  }

 private:
  inline void StartSettingsTimeoutTimer() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsTimeoutManager::StartSettingsTimeoutTimer "
           "did_register_waker_ "
        << did_register_waker_
        << " number_of_acks_unprocessed_ : " << number_of_acks_unprocessed_;
    DCHECK_EQ(number_of_acks_unprocessed_, 0);
    DCHECK(!did_register_waker_);
    sent_time_ = Timestamp::Now();
  }
  inline bool DidReceiveAck() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsTimeoutManager::DidReceiveAck did_register_waker_ "
        << did_register_waker_
        << " number_of_acks_unprocessed_ : " << number_of_acks_unprocessed_;
    return number_of_acks_unprocessed_ > 0;
  }
  inline void AddWaitingForAck() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsTimeoutManager::AddWaitingForAck did_register_waker_ "
        << did_register_waker_
        << " number_of_acks_unprocessed_ : " << number_of_acks_unprocessed_;
    if (!did_register_waker_) {
      DCHECK_EQ(number_of_acks_unprocessed_, 0);
      waker_ = GetContext<Activity>()->MakeNonOwningWaker();
      did_register_waker_ = true;
    }
    DCHECK(did_register_waker_);
  }
  inline void RecordReceivedAck() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsTimeoutManager::RecordReceivedAck did_register_waker_ "
        << did_register_waker_
        << " number_of_acks_unprocessed_ : " << number_of_acks_unprocessed_;
    DCHECK_EQ(number_of_acks_unprocessed_, 0);
    ++number_of_acks_unprocessed_;
    if (did_register_waker_) {
      // It is possible that we receive the ACK before WaitForSettingsTimeout is
      // scheduled. That is why we do this inside an if.
      waker_.Wakeup();
      did_register_waker_ = false;
    }
    DCHECK(!did_register_waker_);
  }
  inline void RemoveReceivedAck() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsTimeoutManager::RemoveReceivedAck did_register_waker_ "
        << did_register_waker_
        << " number_of_acks_unprocessed_ : " << number_of_acks_unprocessed_;
    --number_of_acks_unprocessed_;
    DCHECK_EQ(number_of_acks_unprocessed_, 0);
    DCHECK(!did_register_waker_);
  }

  Duration timeout_;
  // We don't actually use this for the timeout. We are just keeping this as
  // book keeping for better debuggability.
  Timestamp sent_time_ = Timestamp::InfFuture();
  Waker waker_;
  bool did_register_waker_ = false;
  int number_of_acks_unprocessed_ = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_MANAGER_H
