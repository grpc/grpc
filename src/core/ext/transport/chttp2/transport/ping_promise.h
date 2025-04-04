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
#ifndef THIRD_PARTY_GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_PROMISE_H_
#define THIRD_PARTY_GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_PROMISE_H_

#include <memory>

#include "absl/random/random.h"
#include "src/core/ext/transport/chttp2/transport/ping_abuse_policy.h"
#include "src/core/ext/transport/chttp2/transport/ping_callbacks.h"
#include "src/core/ext/transport/chttp2/transport/ping_rate_policy.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace http2 {
class PingSystemInterface {
 public:
  struct SendPingArgs {
    bool ack;
    uint64_t ping_id;
  };
  virtual Promise<absl::Status> SendPing(SendPingArgs args) = 0;
  virtual Promise<absl::Status> TriggerWrite() = 0;
  virtual Promise<absl::Status> PingTimeout() = 0;
  virtual ~PingSystemInterface() = default;
};

class PingSystem {
  using SendPingArgs = PingSystemInterface::SendPingArgs;
  using Callback = absl::AnyInvocable<void()>;
  class PingPromiseCallbacks {
    Chttp2PingCallbacks ping_callbacks_;
    absl::BitGen bitgen_;

   public:
    // Returns a promise that resolves once a new ping is initiated and ack is
    // received for the same. This promise MUST be spawned on the same party as
    // the one that processes the ping ack.
    Promise<absl::Status> RequestPing(Callback on_initiate);

    // Returns a promise that resolves once the next valid ping ack is received.
    // This promise MUST be spawned on the same party as the one that processes
    // the ping ack.
    Promise<absl::Status> WaitForPingAck();

    // Cancells all the callbacks for the inflight pings. This function does not
    // cancel the promises that are waiting on the ping ack.
    void Cancel(grpc_event_engine::experimental::EventEngine* event_engine) {
      // TODO(akshitpatel) : [PH2][P0] : Discuss if we can explore passing event
      // engine as a nullptr.
      ping_callbacks_.CancelAll(event_engine);
    }
    uint64_t StartPing() { return ping_callbacks_.StartPing(bitgen_); }
    bool PingRequested() { return ping_callbacks_.ping_requested(); }
    bool AckPing(uint64_t id,
                 grpc_event_engine::experimental::EventEngine* event_engine) {
      return ping_callbacks_.AckPing(id, event_engine);
    }
    uint64_t CountPingInflight() { return ping_callbacks_.pings_inflight(); }
  };

 private:
  PingPromiseCallbacks ping_callbacks_;
  Chttp2PingAbusePolicy ping_abuse_policy_;
  Chttp2PingRatePolicy ping_rate_policy_;
  bool delayed_ping_spawned_ = false;
  bool is_client_;
  std::unique_ptr<PingSystemInterface> ping_interface_;

  void TriggerDelayedPing(Duration wait, Party* party);
  bool NeedToPing(Duration next_allowed_ping_interval, Party* party);
  void SpawnTimeout(Duration ping_timeout, Party* party);

  void SentPing() { ping_rate_policy_.SentPing(); }

 public:
  PingSystem(const ChannelArgs& channel_args, bool is_client,
             std::unique_ptr<PingSystemInterface> ping_interface);

  Promise<absl::Status> MaybeSendPing(Duration next_allowed_ping_interval,
                                      Duration ping_timeout, Party* party);

  // Ping Rate policy wrapper
  void ReceivedDataFrame() { ping_rate_policy_.ReceivedDataFrame(); }

  // Ping abuse policy wrapper
  bool NotifyPingAbusePolicy(const bool transport_idle) {
    return ping_abuse_policy_.ReceivedOnePing(transport_idle);
  }

  void ResetPingClock() {
    if (!is_client_) {
      ping_abuse_policy_.ResetPingStrikes();
    }
    ping_rate_policy_.ResetPingsBeforeDataRequired();
  }

  // Ping callbacks wrapper
  auto RequestPing(Callback on_initiate) {
    return ping_callbacks_.RequestPing(std::move(on_initiate));
  }
  auto WaitForPingAck() { return ping_callbacks_.WaitForPingAck(); }

  auto Cancel(grpc_event_engine::experimental::EventEngine* event_engine) {
    ping_callbacks_.Cancel(event_engine);
  }

  auto StartPing() { return ping_callbacks_.StartPing(); }
  auto PingRequested() { return ping_callbacks_.PingRequested(); }
  auto AckPing(uint64_t id,
               grpc_event_engine::experimental::EventEngine* event_engine) {
    return ping_callbacks_.AckPing(id, event_engine);
  }
  auto CountPingInflight() { return ping_callbacks_.CountPingInflight(); }
};
}  // namespace http2
}  // namespace grpc_core

#endif  // THIRD_PARTY_GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_PROMISE_H_
