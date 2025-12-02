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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_PROMISES_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_PROMISES_H

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/channelz/property_list.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/time.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/types/span.h"

namespace grpc_core {

// Timeout for getting an ack back on settings changes
#define GRPC_ARG_SETTINGS_TIMEOUT "grpc.http2.settings_timeout"

#define GRPC_SETTINGS_TIMEOUT_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

// This class can only be used only from a promise based HTTP2 transports
// general_party_ .
// This class is designed with the assumption that only 1 SETTINGS frame will be
// in flight at a time. And we do not send a second SETTINGS frame till we
// receive and process the SETTINGS ACK.
class SettingsPromiseManager : public RefCounted<SettingsPromiseManager> {
  // TODO(tjagtap) [PH2][P1][Settings] : Add new DCHECKs
  // TODO(tjagtap) [PH2][P1][Settings] : Refactor full class
 public:
  //////////////////////////////////////////////////////////////////////////////
  // Functions for SETTINGS being sent from our transport to the peer.

  // Assumption : This would be set only once in the life of the transport.
  inline void SetSettingsTimeout(const ChannelArgs& channel_args,
                                 const Duration keepalive_timeout) {
    settings_ack_timeout_ =
        channel_args.GetDurationFromIntMillis(GRPC_ARG_SETTINGS_TIMEOUT)
            .value_or(std::max(keepalive_timeout * 2, Duration::Minutes(1)));
  }

  // Called when transport receives a SETTINGS ACK frame from peer.
  // This SETTINGS ACK was sent by peer to confirm receipt of SETTINGS frame
  // sent by us.
  inline void OnSettingsAckReceived() { RecordReceivedAck(); }

  // Called when our transport enqueues a SETTINGS frame to send to the peer.
  // However, the enqueued frames have not yet been written to the endpoint.
  void WillSendSettings() { should_wait_for_settings_ack_ = true; }

  // Returns true if we should spawn WaitForSettingsTimeout promise.
  bool ShouldSpawnWaitForSettingsTimeout() const {
    return should_wait_for_settings_ack_;
  }

  void TestOnlyTimeoutWaiterSpawned() { TimeoutWaiterSpawned(); }

  // This returns a promise which must be spawned on transports general
  // party. This must be spawned soon after the transport sends a SETTINGS
  // frame on the endpoint. If we don't get an ACK before timeout, the
  // caller MUST close the transport.
  auto WaitForSettingsTimeout() {
    TimeoutWaiterSpawned();
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsPromiseManager::WaitForSettingsTimeout Factory timeout_"
        << settings_ack_timeout_;
    StartSettingsTimeoutTimer();
    return AssertResultType<absl::Status>(Race(
        [self = this->Ref()]() -> Poll<absl::Status> {
          GRPC_SETTINGS_TIMEOUT_DLOG
              << "SettingsPromiseManager::WaitForSettingsTimeout Race";
          // This Promise will "win" the race if we receive the SETTINGS
          // ACK from the peer within the timeout time.
          if (self->HasReceivedAck()) {
            GRPC_DCHECK(
                self->sent_time_ +
                    (self->settings_ack_timeout_ *
                     1.2 /* Grace time for this promise to be scheduled*/) >
                Timestamp::Now())
                << "Should have timed out";
            self->MarkReceivedAckAsProcessed();
            return absl::OkStatus();
          }
          self->AddWaitingForAck();
          return Pending{};
        },
        // This promise will "Win" the Race if timeout is crossed and we did
        // not receive the ACK. The transport must close when this happens.
        TrySeq(Sleep(settings_ack_timeout_),
               [sent_time = sent_time_, timeout = settings_ack_timeout_]() {
                 GRPC_SETTINGS_TIMEOUT_DLOG
                     << "SettingsPromiseManager::WaitForSettingsTimeout Timeout"
                        " triggered. Transport will close. Sent Time : "
                     << sent_time << " Timeout Time : " << (sent_time + timeout)
                     << " Current Time " << Timestamp::Now();
                 return absl::CancelledError(
                     std::string(RFC9113::kSettingsTimeout));
               })));
  }

  //////////////////////////////////////////////////////////////////////////////
  // Functions for SETTINGS being received from the peer.

  // Buffers SETTINGS frames received from peer.
  // Buffered to apply settings at start of next write cycle, only after
  // SETTINGS ACK is written to the endpoint.
  void BufferPeerSettings(std::vector<Http2SettingsFrame::Setting>&& settings) {
    pending_peer_settings_.reserve(pending_peer_settings_.size() +
                                   settings.size());
    pending_peer_settings_.insert(pending_peer_settings_.end(),
                                  settings.begin(), settings.end());
  };

  // Returns settings buffered by BufferPeerSettings().
  // Should be called at start of write cycle, after the SETTINGS ACK has been
  // written to apply the settings. The return value MUST be used.
  std::vector<Http2SettingsFrame::Setting> TakeBufferedPeerSettings() {
    return std::exchange(pending_peer_settings_, {});
  }

  // Appends SETTINGS and SETTINGS ACK frames to output_buf if needed.
  // A SETTINGS frame is appended if local settings changed.
  // SETTINGS ACK frames are appended for any incoming settings that need
  // acknowledgment.
  void MaybeGetSettingsAndSettingsAckFrames(
      chttp2::TransportFlowControl& flow_control, SliceBuffer& output_buf) {
    GRPC_SETTINGS_TIMEOUT_DLOG << "MaybeGetSettingsAndSettingsAckFrames";
    std::optional<Http2Frame> settings_frame = settings_.MaybeSendUpdate();
    if (settings_frame.has_value()) {
      GRPC_SETTINGS_TIMEOUT_DLOG
          << "MaybeGetSettingsAndSettingsAckFrames Frame Settings ";
      Serialize(absl::Span<Http2Frame>(&settings_frame.value(), 1), output_buf);
      flow_control.FlushedSettings();
      WillSendSettings();
    }
    const uint32_t num_acks = settings_.MaybeSendAck();
    if (num_acks > 0) {
      std::vector<Http2Frame> ack_frames(num_acks);
      for (uint32_t i = 0; i < num_acks; ++i) {
        ack_frames[i] = Http2SettingsFrame{true, {}};
      }
      Serialize(absl::MakeSpan(ack_frames), output_buf);
      GRPC_SETTINGS_TIMEOUT_DLOG << "Sending " << num_acks
                                 << " settings ACK frames";
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Wrappers around Http2SettingsManager

  void OnSettingsReceived() { settings_.OnSettingsReceived(); }

  Http2Settings& mutable_local() { return settings_.mutable_local(); }
  Http2Settings& mutable_peer() { return settings_.mutable_peer(); }

  const Http2Settings& local() const { return settings_.local(); }
  const Http2Settings& acked() const { return settings_.acked(); }
  const Http2Settings& peer() const { return settings_.peer(); }

  channelz::PropertyGrid ChannelzProperties() const {
    return settings_.ChannelzProperties();
  }

  http2::Http2ErrorCode ApplyIncomingSettings(
      const std::vector<Http2SettingsFrame::Setting>& settings) {
    return settings_.ApplyIncomingSettings(settings);
  }

  GRPC_MUST_USE_RESULT bool AckLastSend() { return settings_.AckLastSend(); }

  GRPC_MUST_USE_RESULT bool IsPreviousSettingsPromiseResolved() const {
    return settings_.IsPreviousSettingsPromiseResolved();
  }
  void SetPreviousSettingsPromiseResolved(const bool value) {
    settings_.SetPreviousSettingsPromiseResolved(value);
  }

 private:
  Http2SettingsManager settings_;

  //////////////////////////////////////////////////////////////////////////////
  // Functions for SETTINGS being sent from our transport to the peer.
  void TimeoutWaiterSpawned() { should_wait_for_settings_ack_ = false; }
  inline void StartSettingsTimeoutTimer() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsPromiseManager::StartSettingsTimeoutTimer "
           "did_register_waker_ "
        << did_register_ack_timeout_waker_
        << " number_of_acks_unprocessed_ : " << number_of_acks_unprocessed_;
    GRPC_DCHECK_EQ(number_of_acks_unprocessed_, 0);
    GRPC_DCHECK(!did_register_ack_timeout_waker_);
    sent_time_ = Timestamp::Now();
  }

  inline bool HasReceivedAck() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsPromiseManager::DidReceiveAck did_register_waker_ "
        << did_register_ack_timeout_waker_
        << " number_of_acks_unprocessed_ : " << number_of_acks_unprocessed_;
    return number_of_acks_unprocessed_ > 0;
  }
  inline void AddWaitingForAck() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsPromiseManager::AddWaitingForAck did_register_waker_ "
        << did_register_ack_timeout_waker_
        << " number_of_acks_unprocessed_ : " << number_of_acks_unprocessed_;
    if (!did_register_ack_timeout_waker_) {
      GRPC_DCHECK_EQ(number_of_acks_unprocessed_, 0);
      ack_timeout_waker_ = GetContext<Activity>()->MakeNonOwningWaker();
      did_register_ack_timeout_waker_ = true;
    }
    GRPC_DCHECK(did_register_ack_timeout_waker_);
  }
  inline void RecordReceivedAck() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsPromiseManager::RecordReceivedAck did_register_waker_ "
        << did_register_ack_timeout_waker_
        << " number_of_acks_unprocessed_ : " << number_of_acks_unprocessed_;
    GRPC_DCHECK_EQ(number_of_acks_unprocessed_, 0);
    ++number_of_acks_unprocessed_;
    if (did_register_ack_timeout_waker_) {
      ack_timeout_waker_.Wakeup();
      did_register_ack_timeout_waker_ = false;
    } else {
      GRPC_SETTINGS_TIMEOUT_DLOG
          << "We receive the ACK before WaitForSettingsTimeout promise was "
             "scheduled.";
    }
    GRPC_DCHECK(!did_register_ack_timeout_waker_);
  }
  inline void MarkReceivedAckAsProcessed() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsPromiseManager::RemoveReceivedAck did_register_waker_ "
        << did_register_ack_timeout_waker_
        << " number_of_acks_unprocessed_ : " << number_of_acks_unprocessed_;
    --number_of_acks_unprocessed_;
    GRPC_DCHECK_EQ(number_of_acks_unprocessed_, 0);
    GRPC_DCHECK(!did_register_ack_timeout_waker_);
  }

  //////////////////////////////////////////////////////////////////////////////
  // Data Members for SETTINGS being sent from our transport to the peer.
  Duration settings_ack_timeout_;
  // TODO(tjagtap) [PH2][P3][Settings] Delete sent_time_. We don't actually use
  // sent_time_ for the timeout. We are just keeping this as book keeping for
  // better debuggability.
  Timestamp sent_time_ = Timestamp::InfFuture();
  Waker ack_timeout_waker_;
  bool did_register_ack_timeout_waker_ = false;
  int number_of_acks_unprocessed_ = 0;
  bool should_wait_for_settings_ack_ = false;

  //////////////////////////////////////////////////////////////////////////////
  // Data Members for SETTINGS being received from the peer.
  std::vector<Http2SettingsFrame::Setting> pending_peer_settings_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_PROMISES_H
