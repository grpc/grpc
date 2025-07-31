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
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"

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

  // Call when we receive an ACK from our peer.
  // This function is not idempotent.
  GRPC_MUST_USE_RESULT bool AckLastSend();

 private:
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
};

// Timeout for getting an ack back on settings changes
#define GRPC_ARG_SETTINGS_TIMEOUT "grpc.http2.settings_timeout"

// We expect this entire class to be used only from a transports general_party_
// Write usage after Akshit finishes the write promise.
class SettingsTimeoutManager {
 public:
  // Assumption : This would be set only once in the life of the transport.
  // Changing it in the middle of an ongoing timeout is going to chaotic.
  void SetSettingsTimeout(const ChannelArgs& channel_args,
                          const Duration keepalive_timeout) {
    timeout_ =
        channel_args.GetDurationFromIntMillis(GRPC_ARG_SETTINGS_TIMEOUT)
            .value_or(std::max(keepalive_timeout * 2, Duration::Minutes(1)));
    ;
  }

  // To be called when our Transport receives an a SETTINGS ACK frame.
  void OnSettingsAckReceived() { RecordReceived(); }

  // This returns a promise which must be spawned on transports general party.
  // This must be spawned soon after the transport sends a SETTINGS frame on the
  // endpoint.
  // If we don't get an ACK before timeout, the caller MUST close the transport.
  auto WaitForSettingsTimeout() {
    StartSettingsTimeoutTimer();
    return AssertResultType<absl::Status>(
        Race(
            [this]() -> Poll<absl::Status> {
              // This Promise will "win" the race if we receive the SETTINGS
              // ACK from the peer within the timeout time.
              if (DidReceiveAck()) {
                DCHECK(sent_time_ +
                    (timeout_ *
                     1.2 /* 20% grace time for this promise to be scheduled*/) >
                   Timestamp::Now())
                << "Should have timed out";
                RemoveReceived();
                return absl::OkStatus();
              }
              AddWaitingForAck();
              return Pending{};
            },
            // This promise will "Win" the Race if timeout is crossed and we did
            // not receive the ACK. The transport must close when this happens.
            TrySeq(Sleep(timeout_),
                   [sent_time = sent_time_, timeout = timeout_]() {
                     LOG(ERROR) << "Settings timeout triggered. Transport "
                                   "will close. Sent Time : "
                                << sent_time << " Timeout : " << timeout
                                << " Current Time " << Timestamp::Now();
                     return absl::CancelledError("Settings timeout triggered");
                   })));
  }

 private:
  void StartSettingsTimeoutTimer() { sent_time_ = Timestamp::Now(); }
  bool DidReceiveAck() { return number_of_acks_unprocessed_ > 0; }
  void AddWaitingForAck() {
    ++number_of_acks_pending_;
    wakers_.push(GetContext<Activity>()->MakeNonOwningWaker());
  }
  void RecordReceived() {
    if (number_of_acks_pending_ && !wakers_.empty()) {
      DCHECK_GE(number_of_acks_unprocessed_, 0);
      --number_of_acks_pending_;
      wakers_.front().Wakeup();
    }
    ++number_of_acks_unprocessed_;
  }
  void RemoveReceived() {
    --number_of_acks_unprocessed_;
    if (!wakers_.empty()) {
      // It could be that we never needed to register a waker because the
      // promise resolved in the first pass.
      wakers_.pop();
    }
  }

  Duration timeout_;
  Timestamp sent_time_ = Timestamp::InfFuture();
  int number_of_acks_pending_ = 0;
  int number_of_acks_unprocessed_ = 0;
  std::queue<Waker> wakers_;

  /*
  Cases Considered :

  Case 1 :

  WriteLoop Promise
  1. Settings frame is sent out on the wire, followed by a very large payload.
  2. Because of the large payload endpoint_.WriteLoop Promise returns Pending{}.
  Peer :
  3. In the meantime, the peer sees SETTINGS frame and sends the SETTINGS ACK
  ReadLoop Promise :
  4. WriteLoop promise is still waiting on endpoint_.Write . And we read the
     settings ack frame from ReadLoop. And we process it.
  WaitForSettingsTimeout Promise is Run
  5. After some time WriteLoop Promise completes and WaitForSettingsTimeout is
     spawned and then called.

  Dry Run of the above steps (Step by step):
  1. number_of_acks_pending_ = 0 and number_of_acks_unprocessed_ = 0
  2. number_of_acks_pending_ = 0 and number_of_acks_unprocessed_ = 0
  3. number_of_acks_pending_ = 0 and number_of_acks_unprocessed_ = 0
  4. ReadLoop Promise Calls RecordReceived :
     Before : number_of_acks_pending_ = 0 and number_of_acks_unprocessed_ = 0
     After :  number_of_acks_pending_ = 0 and number_of_acks_unprocessed_ = 1
  5. endpoint_.Write completes. WaitForSettingsTimeout() is called.
     DidReceiveAck() is called. Since number_of_acks_unprocessed_ is 1, this
     returns true. RemoveReceived() is called number_of_acks_unprocessed_
     becomes 0. Waker is empty so it is not cleared.
     End State : number_of_acks_pending_ = 0 and number_of_acks_unprocessed_ = 0

  Case 2 :

  WriteLoop Promise
  1. Settings frame is sent out on wire
  1st WaitForSettingsTimeout Promise is Spawned and Run
  2. Settings frame goes out and WaitForSettingsTimeout() is called. It returns
     pending.
  ReadLoop
  3. ReadLoop received the SETTINGS ACK . It updates the state in
     HTTP2SettingsManager. And calls OnSettingsAckReceived. It wakes up the 1st
     WaitForSettingsTimeout. But this promise is not yet
     scheduled for running on the party.
  WriteLoop Promise
  4. Sends another SETTINGS frame to the endpoint
  2nd WaitForSettingsTimeout Promise is Spawned and Run
  5. Settings frame goes out and 2nd WaitForSettingsTimeout() is called. It
  returns pending.
  1st WaitForSettingsTimeout Promise is woken and run Run
  6. It Resolves
  ReadLoop
  7. Gets another SETTINGS ACK. This calls OnSettingsAckReceived.
  2nd WaitForSettingsTimeout Promise is woken and run Run
  8. It Resolves

  Dry Run of the above steps (Step by step):

  1. number_of_acks_pending_ = 0 and number_of_acks_unprocessed_ = 0
  2. number_of_acks_pending_ = 1 and number_of_acks_unprocessed_ = 0
  3. number_of_acks_pending_ = 0 and number_of_acks_unprocessed_ = 1
  4. number_of_acks_pending_ = 0 and number_of_acks_unprocessed_ = 1
  5. number_of_acks_pending_ = 1 and number_of_acks_unprocessed_ = 1
  6. Begin : number_of_acks_pending_ = 1 and number_of_acks_unprocessed_ = 1
     End : number_of_acks_pending_ = 1 and number_of_acks_unprocessed_ = 0
  7. number_of_acks_pending_ = 0 and number_of_acks_unprocessed_ = 1
  8. number_of_acks_pending_ = 0 and number_of_acks_unprocessed_ = 0

*/
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_HTTP2_SETTINGS_MANAGER_H
