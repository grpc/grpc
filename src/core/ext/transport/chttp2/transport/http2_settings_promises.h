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

#include <grpc/event_engine/event_engine.h>
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
#include "src/core/ext/transport/chttp2/transport/write_cycle.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/time.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
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
// receive and process the SETTINGS ACK and resolve the ACK promise.
class SettingsPromiseManager final : public RefCounted<SettingsPromiseManager> {
 public:
  explicit SettingsPromiseManager(
      absl::AnyInvocable<void(absl::StatusOr<uint32_t>)> on_receive_settings)
      : on_receive_first_settings_(std::move(on_receive_settings)),
        state_(SettingsState::kWaitingForFirstPeerSettings) {}

  ~SettingsPromiseManager() override {
    GRPC_DCHECK(on_receive_first_settings_ == nullptr);
  }

  // Not copyable, movable or assignable.
  SettingsPromiseManager(const SettingsPromiseManager&) = delete;
  SettingsPromiseManager& operator=(const SettingsPromiseManager&) = delete;
  SettingsPromiseManager(SettingsPromiseManager&&) = delete;
  SettingsPromiseManager& operator=(SettingsPromiseManager&&) = delete;

  void HandleTransportShutdown(
      grpc_event_engine::experimental::EventEngine* event_engine) {
    // If some scenario causes the transport to close without ever receiving
    // settings, we need to still invoke the closure passed to the transport.
    // Additionally, as this function will always run on the transport party, it
    // cannot race with reading a settings frame.
    // TODO(akshitpatel): [PH2][P4] Pass the actual error that caused the
    // transport to be closed here.
    MaybeReportInitialSettingsAbort(event_engine);
  }

  bool IsFirstPeerSettingsApplied() const {
    return state_ == SettingsState::kReady;
  }

  //////////////////////////////////////////////////////////////////////////////
  // Functions for SETTINGS being sent from our transport to the peer.

  // Assumption : This would be set only once in the life of the transport.
  inline void SetSettingsTimeout(const Duration timeout) {
    GRPC_DCHECK(state_ == SettingsState::kWaitingForFirstPeerSettings);
    settings_ack_timeout_ = timeout;
  }

  // Called when transport receives a SETTINGS ACK frame from peer.
  // This SETTINGS ACK was sent by peer to confirm receipt of SETTINGS frame
  // sent by us. Stop the settings timeout promise.
  GRPC_MUST_USE_RESULT bool OnSettingsAckReceived() {
    bool is_valid = settings_.AckLastSend();
    if (is_valid) {
      RecordReceivedAck();
    }
    return is_valid;
  }

  // Called when our transport enqueues a SETTINGS frame to send to the peer.
  // However, the enqueued frames have not yet been written to the endpoint.
  void WillSendSettings() {
    GRPC_DCHECK(!should_wait_for_settings_ack_);
    should_wait_for_settings_ack_ = true;
  }

  // Returns true if we should spawn WaitForSettingsTimeout promise.
  bool ShouldSpawnWaitForSettingsTimeout() const {
    return should_wait_for_settings_ack_;
  }

  // This returns a promise which must be spawned on transports general
  // party. This must be spawned soon after the transport sends a SETTINGS
  // frame on the endpoint. If we don't get an ACK before timeout, the
  // caller MUST close the transport.
  auto WaitForSettingsTimeout() {
    did_previous_settings_promise_resolve_ = false;
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
            self->did_previous_settings_promise_resolve_ = true;
            return absl::OkStatus();
          }
          self->AddWaitingForAck();
          return Pending{};
        },
        // This promise will "Win" the Race if timeout is crossed and we did
        // not receive the ACK. The transport must close when this happens.
        TrySeq(Sleep(settings_ack_timeout_),
               [sent_time = sent_time_, timeout = settings_ack_timeout_]() {
                 const std::string message = absl::StrCat(
                     RFC9113::kSettingsTimeout,
                     " Sent Time : ", sent_time.ToString(),
                     " Timeout Time : ", (sent_time + timeout).ToString(),
                     " Current Time : ", Timestamp::Now().ToString());
                 GRPC_SETTINGS_TIMEOUT_DLOG
                     << "SettingsPromiseManager::WaitForSettingsTimeout"
                     << message;
                 // Ideally we must set did_previous_settings_promise_resolve_
                 // to false, but in this case the transport will be closed so
                 // it does not matter. I am trying to avoid taking another ref
                 // on self in this TrySeq.
                 return absl::CancelledError(message);
               })));
  }

  void TestOnlyRecordReceivedAck() { RecordReceivedAck(); }
  void TestOnlyTimeoutWaiterSpawned() { TimeoutWaiterSpawned(); }

  //////////////////////////////////////////////////////////////////////////////
  // Functions for SETTINGS being received from the peer.

  // Buffers SETTINGS frames received from peer.
  // Buffered to apply settings at start of next write cycle, only after
  // SETTINGS ACK is written to the endpoint.
  void BufferPeerSettings(std::vector<Http2SettingsFrame::Setting>&& settings) {
    if (state_ == SettingsState::kWaitingForFirstPeerSettings) {
      state_ = SettingsState::kFirstPeerSettingsReceived;
    }
    ++num_acks_to_send_;
    pending_peer_settings_.reserve(pending_peer_settings_.size() +
                                   settings.size());
    pending_peer_settings_.insert(pending_peer_settings_.end(),
                                  settings.begin(), settings.end());
  }

  // Applies settings buffered by BufferPeerSettings().
  // Should be called at start of write cycle, after the SETTINGS ACK has been
  // written to apply the settings. If the first settings frame is received from
  // the peer that that needs some special handling too.
  http2::Http2ErrorCode MaybeReportAndApplyBufferedPeerSettings(
      grpc_event_engine::experimental::EventEngine* event_engine,
      bool& should_spawn_security_frame_loop) {
    http2::Http2ErrorCode status = settings_.ApplyIncomingSettings(
        std::exchange(pending_peer_settings_, {}));
    if (state_ == SettingsState::kFirstPeerSettingsReceived) {
      MaybeReportInitialSettings(event_engine);
      state_ = SettingsState::kReady;
      should_spawn_security_frame_loop = IsSecurityFrameExpected();
    }
    return status;
  }

  //////////////////////////////////////////////////////////////////////////////
  // Wrappers around Http2SettingsManager

  // Appends SETTINGS and SETTINGS ACK frames to output_buf if needed.
  // A SETTINGS frame is appended if local settings changed.
  // SETTINGS ACK frames are appended for any incoming settings that need
  // acknowledgment. This MUST be called only after the
  // MaybeReportAndApplyBufferedPeerSettings function.
  void MaybeGetSettingsAndSettingsAckFrames(
      chttp2::TransportFlowControl& flow_control,
      http2::FrameSender& frame_sender) {
    GRPC_SETTINGS_TIMEOUT_DLOG << "MaybeGetSettingsAndSettingsAckFrames";
    if (did_previous_settings_promise_resolve_) {
      std::optional<Http2Frame> settings_frame = settings_.MaybeSendUpdate();
      if (settings_frame.has_value()) {
        GRPC_SETTINGS_TIMEOUT_DLOG
            << "MaybeGetSettingsAndSettingsAckFrames Frame Settings ";
        frame_sender.AddRegularFrame(std::move(*settings_frame));
        flow_control.FlushedSettings();
        WillSendSettings();
      }
    }
    if (num_acks_to_send_ > 0) {
      GRPC_SETTINGS_TIMEOUT_DLOG << "Sending " << num_acks_to_send_
                                 << " settings ACK frames";
      frame_sender.ReserveRegularFrames(num_acks_to_send_);
      for (uint32_t i = 0; i < num_acks_to_send_; ++i) {
        frame_sender.AddRegularFrame(Http2SettingsFrame{true, {}});
      }

      num_acks_to_send_ = 0;
    }
  }

  Http2Settings& mutable_local() { return settings_.mutable_local(); }
  Http2Settings& mutable_peer() { return settings_.mutable_peer(); }

  const Http2Settings& local() const { return settings_.local(); }
  const Http2Settings& acked() const { return settings_.acked(); }
  const Http2Settings& peer() const { return settings_.peer(); }

  //////////////////////////////////////////////////////////////////////////////
  // ChannelZ and Security Frame Stuff

  channelz::PropertyGrid ChannelzProperties() const {
    return settings_.ChannelzProperties();
  }

  bool IsSecurityFrameExpected() const {
    GRPC_DCHECK(IsFirstPeerSettingsApplied())
        << "Security frame must not be received before SETTINGS frame";
    // TODO(tjagtap) : [PH2][P3] : Evaluate when to accept the frame and when to
    // reject it. Compare it with the requirement and with CHTTP2.
    return (settings_.local().allow_security_frame()) &&
           settings_.peer().allow_security_frame();
  };

 private:
  Http2SettingsManager settings_;

  //////////////////////////////////////////////////////////////////////////////
  // Plumbing Settings with Chttp2Connector class

  void MaybeReportInitialSettings(
      grpc_event_engine::experimental::EventEngine* event_engine) {
    // TODO(tjagtap) [PH2][P2] Relook at this while writing server. I think this
    // will be different for client and server.
    if (on_receive_first_settings_ != nullptr) {
      GRPC_DCHECK(state_ == SettingsState::kFirstPeerSettingsReceived);
      GRPC_DCHECK(event_engine != nullptr);
      event_engine->Run(
          [on_receive_settings = std::move(on_receive_first_settings_),
           peer_max_concurrent_streams =
               settings_.peer().max_concurrent_streams()]() mutable {
            ExecCtx exec_ctx;
            on_receive_settings(peer_max_concurrent_streams);
            // Ensure the captured callback is destroyed while ExecCtx is still
            // alive. Its destructor may trigger work that needs to schedule
            // closures on the ExecCtx.
            on_receive_settings = nullptr;
          });
      GRPC_DCHECK(on_receive_first_settings_ == nullptr);
    }
  }

  void MaybeReportInitialSettingsAbort(
      grpc_event_engine::experimental::EventEngine* event_engine) {
    // TODO(tjagtap) [PH2][P2] Relook at this while writing server. I think this
    // will be different for client and server.
    if (on_receive_first_settings_ != nullptr) {
      GRPC_DCHECK(event_engine != nullptr);
      GRPC_DCHECK(state_ != SettingsState::kReady);
      event_engine->Run([on_receive_settings =
                             std::move(on_receive_first_settings_)]() mutable {
        ExecCtx exec_ctx;
        on_receive_settings(absl::UnavailableError("transport closed"));
        // Ensure the captured callback is destroyed while ExecCtx is still
        // alive. Its destructor may trigger work that needs to schedule
        // closures on the ExecCtx.
        on_receive_settings = nullptr;
      });
      GRPC_DCHECK(on_receive_first_settings_ == nullptr);
    }
  }

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
  // TODO(tjagtap) [PH2][P5][Settings] Delete sent_time_. We don't actually use
  // sent_time_ for the timeout. We are just keeping this as book keeping for
  // better debuggability.
  Timestamp sent_time_ = Timestamp::InfFuture();
  Waker ack_timeout_waker_;
  int number_of_acks_unprocessed_ = 0;
  bool did_register_ack_timeout_waker_ = false;
  bool should_wait_for_settings_ack_ = false;

  // For CHTTP2, MaybeSendUpdate() checks `update_state_` to ensure only one
  // SETTINGS frame is in flight at a time. PH2 requires an additional
  // constraint: a new SETTINGS frame cannot be sent until the SETTINGS-ACK
  // timeout promise for the previous frame has resolved. This flag tracks this
  // condition for PH2.
  bool did_previous_settings_promise_resolve_ = true;

  //////////////////////////////////////////////////////////////////////////////
  // Data Members for SETTINGS being received from the peer.

  absl::AnyInvocable<void(absl::StatusOr<uint32_t>)> on_receive_first_settings_;
  std::vector<Http2SettingsFrame::Setting> pending_peer_settings_;
  // Number of incoming SETTINGS frames that we have received but not ACKed yet.
  uint32_t num_acks_to_send_ = 0;

  enum class SettingsState : uint8_t {
    kWaitingForFirstPeerSettings,
    kFirstPeerSettingsReceived,
    kReady,
  };
  SettingsState state_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_PROMISES_H
