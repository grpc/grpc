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
#include <stdint.h>

#include <cstdint>
#include <optional>
#include <queue>

#include "src/core/channelz/property_list.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings_manager.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/time.h"
#include "src/core/util/useful.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"
namespace grpc_core {

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
                GRPC_DCHECK(
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
    GRPC_DCHECK_EQ(number_of_acks_unprocessed_, 0);
    GRPC_DCHECK(!did_register_waker_);
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
      GRPC_DCHECK_EQ(number_of_acks_unprocessed_, 0);
      waker_ = GetContext<Activity>()->MakeNonOwningWaker();
      did_register_waker_ = true;
    }
    GRPC_DCHECK(did_register_waker_);
  }
  inline void RecordReceivedAck() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsTimeoutManager::RecordReceivedAck did_register_waker_ "
        << did_register_waker_
        << " number_of_acks_unprocessed_ : " << number_of_acks_unprocessed_;
    GRPC_DCHECK_EQ(number_of_acks_unprocessed_, 0);
    ++number_of_acks_unprocessed_;
    if (did_register_waker_) {
      // It is possible that we receive the ACK before WaitForSettingsTimeout is
      // scheduled. That is why we do this inside an if.
      waker_.Wakeup();
      did_register_waker_ = false;
    }
    GRPC_DCHECK(!did_register_waker_);
  }
  inline void RemoveReceivedAck() {
    GRPC_SETTINGS_TIMEOUT_DLOG
        << "SettingsTimeoutManager::RemoveReceivedAck did_register_waker_ "
        << did_register_waker_
        << " number_of_acks_unprocessed_ : " << number_of_acks_unprocessed_;
    --number_of_acks_unprocessed_;
    GRPC_DCHECK_EQ(number_of_acks_unprocessed_, 0);
    GRPC_DCHECK(!did_register_waker_);
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

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_PROMISES_H
