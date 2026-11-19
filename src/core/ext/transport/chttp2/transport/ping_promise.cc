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

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/ping_rate_policy.h"
#include "src/core/ext/transport/chttp2/transport/write_cycle.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/match.h"
#include "src/core/util/time.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/types/span.h"

namespace grpc_core {
namespace http2 {
using Callback = absl::AnyInvocable<void()>;
using grpc_event_engine::experimental::EventEngine;

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

std::optional<PingManager::PingTimeoutArgs>
PingManager::PingPromiseCallbacks::ArmPingTimeout(const uint64_t opaque_data,
                                                  const Duration ping_timeout) {
  std::shared_ptr<InterActivityLatch<void>> timeout_latch =
      std::make_shared<InterActivityLatch<void>>();
  const std::optional<uint64_t> id = ping_callbacks_.OnPingTimeout(
      ping_timeout, event_engine_.get(),
      [timeout_latch]() { timeout_latch->Set(); });
  if (!id.has_value()) {
    // No inflight ping, e.g. the ack for this ping was received before the
    // timeout could be armed. There is nothing to wait for.
    GRPC_HTTP2_PING_LOG << "No inflight ping for id=" << opaque_data
                        << ". Not waiting for ping timeout.";
    return std::nullopt;
  }
  GRPC_DCHECK_EQ(*id, opaque_data);
  GRPC_HTTP2_PING_LOG << "Ping timeout of duration: " << ping_timeout
                      << " armed for ping id=" << opaque_data;
  // Register the ack callback only after confirming the ping is still in
  // flight. Otherwise it would attach to the next ping instead.
  std::shared_ptr<Latch<void>> ack_latch = std::make_shared<Latch<void>>();
  ping_callbacks_.OnPingAck([ack_latch]() { ack_latch->Set(); });
  return PingTimeoutArgs{opaque_data, std::move(timeout_latch),
                         std::move(ack_latch)};
}

// Ping System implementation
PingManager::PingManager(const ChannelArgs& channel_args, const bool is_client,
                         Duration ping_timeout,
                         std::unique_ptr<PingInterface> ping_interface,
                         std::shared_ptr<EventEngine> event_engine)
    : ping_callbacks_(event_engine),
      ping_abuse_policy_(channel_args),
      ping_rate_policy_(channel_args, is_client),
      ping_interface_(std::move(ping_interface)),
      ping_timeout_(ping_timeout) {}

PingManager::TriggerPingArgs PingManager::NeedToPing(
    const Duration next_allowed_ping_interval) {
  if (!ping_callbacks_.PingRequested()) {
    return TriggerPingArgs(std::nullopt, /*need_to_ping=*/false);
  }

  return Match(
      ping_rate_policy_.RequestSendPing(next_allowed_ping_interval,
                                        ping_callbacks_.CountPingInflight()),
      [this](Chttp2PingRatePolicy::SendGranted) {
        // TODO(akshitpatel) : [PH2][P1] : Update some keepalive flags.
        GRPC_HTTP2_PING_LOG << "[PH2]: Ping sent: "
                            << ping_rate_policy_.GetDebugString();
        return TriggerPingArgs(std::nullopt, /*need_to_ping=*/true);
      },
      [this](Chttp2PingRatePolicy::TooManyRecentPings) {
        GRPC_HTTP2_PING_LOG << "[PH2]: Ping delayed too many recent pings: "
                            << ping_rate_policy_.GetDebugString();
        return TriggerPingArgs(std::nullopt, /*need_to_ping=*/false);
      },
      [this](Chttp2PingRatePolicy::TooSoon too_soon) mutable {
        GRPC_HTTP2_PING_LOG
            << "[PH2]: Ping delayed not enough time elapsed since last "
               "ping. Last ping:"
            << too_soon.last_ping
            << ", minimum wait:" << too_soon.next_allowed_ping_interval
            << ", need to wait:" << too_soon.wait;
        std::optional<Duration> delayed_ping_wait;
        if (!std::exchange(delayed_ping_spawned_, true)) {
          delayed_ping_wait = too_soon.wait;
        }
        return TriggerPingArgs(delayed_ping_wait, /*need_to_ping=*/false);
      });
}

std::optional<Duration> PingManager::MaybeGetSerializedPingFrames(
    FrameSender& frame_sender, Duration next_allowed_ping_interval) {
  GRPC_HTTP2_PING_LOG << "PingManager MaybeGetSerializedPingFrames "
                         "pending_ping_acks_ size: "
                      << pending_ping_acks_.size()
                      << " next_allowed_ping_interval: "
                      << next_allowed_ping_interval;
  GRPC_DCHECK(!opaque_data_.has_value());
  frame_sender.ReserveRegularFrames(pending_ping_acks_.size());

  // Get the serialized ping acks if needed.
  for (uint64_t opaque_data : pending_ping_acks_) {
    frame_sender.AddRegularFrame(GetHttp2PingFrame(/*ack=*/true, opaque_data));
  }
  pending_ping_acks_.clear();

  // Get the serialized ping frame if needed.
  TriggerPingArgs trigger_ping_args = NeedToPing(next_allowed_ping_interval);
  if (trigger_ping_args.need_to_ping) {
    const uint64_t opaque_data = ping_callbacks_.StartPing();
    frame_sender.AddRegularFrame(GetHttp2PingFrame(/*ack=*/false, opaque_data));
    opaque_data_ = opaque_data;
    GRPC_HTTP2_PING_LOG << "Created ping frame for id= " << opaque_data;
  }

  return trigger_ping_args.delayed_ping_wait;
}

std::optional<PingManager::PingTimeoutArgs> PingManager::NotifyPingSent() {
  if (!opaque_data_.has_value()) {
    return std::nullopt;
  }
  SentPing();
  return ping_callbacks_.ArmPingTimeout(
      *std::exchange(opaque_data_, std::nullopt), ping_timeout_);
}

void PingManager::AddPendingPingAck(const uint64_t opaque_data) {
  GRPC_HTTP2_PING_LOG << "Adding pending ping ack for id=" << opaque_data
                      << " to the list of pending ping acks.";
  pending_ping_acks_.push_back(opaque_data);
}

}  // namespace http2
}  // namespace grpc_core
