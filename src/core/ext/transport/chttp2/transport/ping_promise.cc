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
#include "src/core/ext/transport/chttp2/transport/ping_promise.h"

#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/match.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace http2 {
// Ping Callback promise based wrappers
Promise<absl::Status> PingSystem::PingPromiseCallbacks::RequestPing(
    Callback on_initiate) {
  // TODO(akshitpatel) : [PH2][P0] : shared_ptr might not be needed. Simply
  // passing a ref to the ping_callback should suffice. The only case that
  // needs some thought is the promise waiting on the latch is cancelled but
  // the ack callback is still called. This should not happen in practice
  // but need to verify.
  std::shared_ptr<Latch<void>> latch = std::make_shared<Latch<void>>();
  auto on_ack = [latch]() { latch->Set(); };
  ping_callbacks_.OnPing(std::move(on_initiate), std::move(on_ack));
  return Map(latch->Wait(), [latch](Empty) { return absl::OkStatus(); });
}
Promise<absl::Status> PingSystem::PingPromiseCallbacks::WaitForPingAck() {
  std::shared_ptr<Latch<void>> latch = std::make_shared<Latch<void>>();
  auto on_ack = [latch]() { latch->Set(); };
  ping_callbacks_.OnPingAck(std::move(on_ack));
  return Map(latch->Wait(), [latch](Empty) { return absl::OkStatus(); });
}

// Ping System implementation
PingSystem::PingSystem(const ChannelArgs& channel_args, bool is_client,
                       std::unique_ptr<PingSystemInterface> ping_interface)
    : ping_callbacks_(),
      ping_abuse_policy_(channel_args),
      ping_rate_policy_(channel_args, /*is_client=*/true),
      is_client_(is_client),
      ping_interface_(std::move(ping_interface)) {}

void PingSystem::TriggerDelayedPing(Duration wait, Party* party) {
  // Spawn at most once.
  if (delayed_ping_spawned_) {
    return;
  }
  delayed_ping_spawned_ = true;
  LOG(INFO) << "Triggering delayed ping with wait: " << wait;
  party->Spawn(
      "DelayedPing",
      [this, wait]() mutable {
        return TrySeq(Sleep(wait), [this]() mutable {
          return ping_interface_->TriggerWrite();
        });
      },
      [this](auto) {
        // TODO(akshitpatel) : [PH2][P1] : Fix logs
        LOG(INFO) << "Triggered delayed ping";
        delayed_ping_spawned_ = false;
      });
}

bool PingSystem::NeedToPing(Duration next_allowed_ping_interval, Party* party) {
  bool send_ping_now = false;
  if (!ping_callbacks_.PingRequested()) {
    LOG(INFO) << "Need to ping returning: " << send_ping_now;
    return send_ping_now;
  }

  Match(
      ping_rate_policy_.RequestSendPing(next_allowed_ping_interval,
                                        ping_callbacks_.CountPingInflight()),
      [&send_ping_now](grpc_core::Chttp2PingRatePolicy::SendGranted) {
        send_ping_now = true;
        // TODO(akshitpatel) : [PH2][P1] : Update some keepalive flags.
        // TODO(akshitpatel) : [PH2][P1] : Logs and Traces
      },
      [](grpc_core::Chttp2PingRatePolicy::TooManyRecentPings) {
        // TODO(akshitpatel) : [PH2][P1] : Logs and Traces
      },
      [this, party](grpc_core::Chttp2PingRatePolicy::TooSoon too_soon) mutable {
        // TODO(akshitpatel) : [PH2][P1] : Logs and Traces
        TriggerDelayedPing(too_soon.wait, party);
      });

  LOG(INFO) << "Need to ping returning2: " << send_ping_now;
  return send_ping_now;
}

void PingSystem::SpawnTimeout(Duration ping_timeout, Party* party) {
  party->Spawn(
      "PingTimeout",
      [this, ping_timeout]() {
        return Race(
            TrySeq(Sleep(ping_timeout),
                   [this]() mutable { return ping_interface_->PingTimeout(); }),
            Map(ping_callbacks_.WaitForPingAck(), [](absl::Status status) {
              LOG(INFO) << "Ping ack received";
              return status;
            }));
      },
      [](auto) { LOG(INFO) << "Timeout ended"; });
}

Promise<absl::Status> PingSystem::MaybeSendPing(
    Duration next_allowed_ping_interval, Duration ping_timeout, Party* party) {
  return If(
      NeedToPing(next_allowed_ping_interval, party),
      [this, ping_timeout, party]() mutable {
        const uint64_t ping_id = ping_callbacks_.StartPing();
        SentPing();
        return TrySeq(ping_interface_->SendPing(SendPingArgs{false, ping_id}),
                      [this, ping_timeout, party]() {
                        // TODO(akshitpatel) : [PH2][P0] : Fix logs
                        LOG(INFO) << "Http2ClientTransport Ping request sent";
                        SpawnTimeout(ping_timeout, party);
                        return absl::OkStatus();
                      });
      },
      []() { return Immediate(absl::OkStatus()); });
}
}  // namespace http2
}  // namespace grpc_core
