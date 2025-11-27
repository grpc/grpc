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

#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/match.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace http2 {
using Callback = absl::AnyInvocable<void()>;
using grpc_event_engine::experimental::EventEngine;

#define GRPC_HTTP2_PING_LOG                                \
  LOG_IF(INFO, (GRPC_TRACE_FLAG_ENABLED(http) ||           \
                GRPC_TRACE_FLAG_ENABLED(bdp_estimator) ||  \
                GRPC_TRACE_FLAG_ENABLED(http_keepalive) || \
                GRPC_TRACE_FLAG_ENABLED(http2_ping)))

Promise<absl::Status> PingManager::PingPromiseCallbacks::RequestPing(
    Callback on_initiate, bool important) {
  important_ping_requested_ = (important_ping_requested_ || important);
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

void PingManager::TriggerDelayedPing(const Duration wait) {
  // Spawn at most once.
  if (delayed_ping_spawned_) {
    return;
  }
  delayed_ping_spawned_ = true;
  GetContext<Party>()->Spawn(
      "DelayedPing",
      // TODO(akshitpatel) : [PH2][P2] : Verify if we need a RefCountedPtr for
      // ping_manager.
      [this, wait]() mutable {
        GRPC_HTTP2_PING_LOG << "Scheduling delayed ping after wait=" << wait;
        return AssertResultType<absl::Status>(TrySeq(
            Sleep(wait),
            [this]() mutable { return ping_interface_->TriggerWrite(); }));
      },
      [this](auto) { delayed_ping_spawned_ = false; });
}

bool PingManager::NeedToPing(const Duration next_allowed_ping_interval) {
  if (!ping_callbacks_.PingRequested()) {
    return false;
  }

  return Match(
      ping_rate_policy_.RequestSendPing(next_allowed_ping_interval,
                                        ping_callbacks_.CountPingInflight()),
      [this](Chttp2PingRatePolicy::SendGranted) {
        // TODO(akshitpatel) : [PH2][P1] : Update some keepalive flags.
        GRPC_HTTP2_PING_LOG << "CLIENT" << "[" << "PH2"
                            << "]: Ping sent"
                            << ping_rate_policy_.GetDebugString();
        return true;
      },
      [this](Chttp2PingRatePolicy::TooManyRecentPings) {
        GRPC_HTTP2_PING_LOG << "CLIENT" << "[" << "PH2"
                            << "]: Ping delayed too many recent pings: "
                            << ping_rate_policy_.GetDebugString();
        return false;
      },
      [this](Chttp2PingRatePolicy::TooSoon too_soon) mutable {
        GRPC_HTTP2_PING_LOG
            << "]: Ping delayed not enough time elapsed since last "
               "ping. Last ping:"
            << too_soon.last_ping
            << ", minimum wait:" << too_soon.next_allowed_ping_interval
            << ", need to wait:" << too_soon.wait;
        TriggerDelayedPing(too_soon.wait);
        return false;
      });
}

void PingManager::SpawnTimeout(const Duration ping_timeout,
                               const uint64_t opaque_data) {
  GetContext<Party>()->Spawn(
      "PingTimeout",
      // TODO(akshitpatel) : [PH2][P2] : Verify if we need a RefCountedPtr for
      // ping_manager.
      [this, ping_timeout, opaque_data]() {
        return AssertResultType<absl::Status>(Race(
            TrySeq(ping_callbacks_.PingTimeout(ping_timeout),
                   [this, opaque_data](bool trigger_ping_timeout) mutable {
                     return If(
                         trigger_ping_timeout,
                         [this, opaque_data]() {
                           GRPC_HTTP2_PING_LOG
                               << " Ping ack not received for id="
                               << opaque_data << ". Ping timeout triggered.";
                           return ping_interface_->PingTimeout();
                         },
                         []() { return absl::OkStatus(); });
                   }),
            ping_callbacks_.WaitForPingAck()));
      },
      [](auto) {});
}

void PingManager::MaybeGetSerializedPingFrames(
    SliceBuffer& output_buffer, const Duration next_allowed_ping_interval) {
  GRPC_HTTP2_PING_LOG << "PingManager MaybeGetSerializedPingFrames "
                         "pending_ping_acks_ size: "
                      << pending_ping_acks_.size()
                      << " next_allowed_ping_interval: "
                      << next_allowed_ping_interval;
  GRPC_DCHECK(!opaque_data_.has_value());
  std::vector<Http2Frame> frames;
  frames.reserve(pending_ping_acks_.size() + 1);  // +1 for the ping frame.

  // Get the serialized ping acks if needed.
  for (uint64_t opaque_data : pending_ping_acks_) {
    frames.emplace_back(GetHttp2PingFrame(/*ack=*/true, opaque_data));
  }
  pending_ping_acks_.clear();

  // Get the serialized ping frame if needed.
  if (NeedToPing(next_allowed_ping_interval)) {
    const uint64_t opaque_data = ping_callbacks_.StartPing();
    frames.emplace_back(GetHttp2PingFrame(/*ack=*/false, opaque_data));
    opaque_data_ = opaque_data;
    GRPC_HTTP2_PING_LOG << "Created ping frame for id= " << opaque_data;
  }

  // Serialize the frames if any.
  if (!frames.empty()) {
    Serialize(absl::Span<Http2Frame>(frames), output_buffer);
  }
}

void PingManager::NotifyPingSent(const Duration ping_timeout) {
  if (opaque_data_.has_value()) {
    SpawnTimeout(ping_timeout, opaque_data_.value());
    SentPing();
    opaque_data_.reset();
  }
}

void PingManager::AddPendingPingAck(const uint64_t opaque_data) {
  GRPC_HTTP2_PING_LOG << "Adding pending ping ack for id=" << opaque_data
                      << " to the list of pending ping acks.";
  pending_ping_acks_.push_back(opaque_data);
}

}  // namespace http2
}  // namespace grpc_core
