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
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/match.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace http2 {
using SendPingArgs = ::grpc_core::http2::PingInterface::SendPingArgs;
using Callback = absl::AnyInvocable<void()>;
using grpc_event_engine::experimental::EventEngine;

#define PING_LOG                                           \
  LOG_IF(INFO, (GRPC_TRACE_FLAG_ENABLED(http) ||           \
                GRPC_TRACE_FLAG_ENABLED(bdp_estimator) ||  \
                GRPC_TRACE_FLAG_ENABLED(http_keepalive) || \
                GRPC_TRACE_FLAG_ENABLED(http2_ping)))

Promise<absl::Status> PingManager::PingPromiseCallbacks::RequestPing(
    Callback on_initiate) {
  std::shared_ptr<Latch<void>> latch = std::make_shared<Latch<void>>();
  auto on_ack = [latch]() { latch->Set(); };
  ping_callbacks_.OnPing(std::move(on_initiate), std::move(on_ack));
  return Map(latch->Wait(), [latch](Empty) { return absl::OkStatus(); });
}

Promise<absl::Status> PingManager::PingPromiseCallbacks::WaitForPingAck() {
  std::shared_ptr<Latch<void>> latch = std::make_shared<Latch<void>>();
  auto on_ack = [latch]() { latch->Set(); };
  ping_callbacks_.OnPingAck(std::move(on_ack));
  return Map(latch->Wait(), [latch](Empty) { return absl::OkStatus(); });
}

// Ping System implementation
PingManager::PingManager(const ChannelArgs& channel_args,
                         std::unique_ptr<PingInterface> ping_interface,
                         std::shared_ptr<EventEngine> event_engine)
    : ping_callbacks_(event_engine),
      ping_abuse_policy_(channel_args),
      ping_rate_policy_(channel_args, /*is_client=*/true),
      ping_interface_(std::move(ping_interface)) {}

void PingManager::TriggerDelayedPing(Duration wait) {
  // Spawn at most once.
  if (delayed_ping_spawned_) {
    return;
  }
  delayed_ping_spawned_ = true;
  GetContext<Party>()->Spawn(
      "DelayedPing",
      [this, wait]() mutable {
        VLOG(2) << "Scheduling delayed ping after wait=" << wait;
        return AssertResultType<absl::Status>(TrySeq(
            Sleep(wait),
            [this]() mutable { return ping_interface_->TriggerWrite(); }));
      },
      [this](auto) { delayed_ping_spawned_ = false; });
}

bool PingManager::NeedToPing(Duration next_allowed_ping_interval) {
  if (!ping_callbacks_.PingRequested()) {
    return false;
  }

  return Match(
      ping_rate_policy_.RequestSendPing(next_allowed_ping_interval,
                                        ping_callbacks_.CountPingInflight()),
      [this](Chttp2PingRatePolicy::SendGranted) {
        // TODO(akshitpatel) : [PH2][P1] : Update some keepalive flags.
        PING_LOG << "CLIENT" << "[" << "PH2"
                 << "]: Ping sent" << ping_rate_policy_.GetDebugString();
        return true;
      },
      [this](Chttp2PingRatePolicy::TooManyRecentPings) {
        PING_LOG << "CLIENT" << "[" << "PH2"
                 << "]: Ping delayed too many recent pings: "
                 << ping_rate_policy_.GetDebugString();
        return false;
      },
      [this](Chttp2PingRatePolicy::TooSoon too_soon) mutable {
        PING_LOG << "]: Ping delayed not enough time elapsed since last "
                    "ping. Last ping:"
                 << too_soon.last_ping
                 << ", minimum wait:" << too_soon.next_allowed_ping_interval
                 << ", need to wait:" << too_soon.wait;
        TriggerDelayedPing(too_soon.wait);
        return false;
      });
}

void PingManager::SpawnTimeout(Duration ping_timeout,
                               const uint64_t opaque_data) {
  GetContext<Party>()->Spawn(
      "PingTimeout",
      [this, ping_timeout, opaque_data]() {
        return AssertResultType<absl::Status>(Race(
            TrySeq(ping_callbacks_.PingTimeout(ping_timeout),
                   [this, opaque_data]() mutable {
                     VLOG(2) << " Ping ack not received for id=" << opaque_data
                             << ". Ping timeout triggered.";
                     return ping_interface_->PingTimeout();
                   }),
            ping_callbacks_.WaitForPingAck()));
      },
      [](auto) {});
}

Promise<absl::Status> PingManager::MaybeSendPing(
    Duration next_allowed_ping_interval, Duration ping_timeout) {
  return If(
      NeedToPing(next_allowed_ping_interval),
      [this, ping_timeout]() mutable {
        const uint64_t opaque_data = ping_callbacks_.StartPing();
        return AssertResultType<absl::Status>(
            TrySeq(ping_interface_->SendPing(SendPingArgs{false, opaque_data}),
                   [this, ping_timeout, opaque_data]() {
                     VLOG(2) << "Ping Sent with id: " << opaque_data;
                     SpawnTimeout(ping_timeout, opaque_data);
                     SentPing();
                     return absl::OkStatus();
                   }));
      },
      []() { return Immediate(absl::OkStatus()); });
}
}  // namespace http2
}  // namespace grpc_core
