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
#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_PROMISE_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_PROMISE_H

#include <memory>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/ping_abuse_policy.h"
#include "src/core/ext/transport/chttp2/transport/ping_callbacks.h"
#include "src/core/ext/transport/chttp2/transport/ping_rate_policy.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace http2 {

// Ping Promise Spawns Overview

// | Promise Spawn   | Max Duration | Promise      | Max Spawns              |
// |                 | for Spawn    | Resolution   |                         |
// |-----------------|--------------|--------------|-------------------------|
// | Ping Timeout    | 1 minute     | On Ping ack  | One per inflight ping   |
// |                 |              | or timeout   |                         |
// | Delayed Ping    | 2 Hours      | On scheduled | One                     |
// |                 |              | time         |                         |
// | Ping Waiter     | 1 minute     | On Ping ack  | One per ping request    |
// |                 |              | or timeout   |                         |
// Max Party Slots:
// - Without multi ping:
//   - 1 per ping request + 1 (for delayed ping) + 1 (for ping timeout)
//   - Worst case(3 ping requests): 5

// - With multi ping:
//   - 1 per ping request + 1 (for delayed ping) + 1 per inflight ping
//                                                (for ping timeout)
//   - Worst case(3 ping requests): 7

class PingInterface {
 public:
  // Returns a promise that triggers a write cycle on the transport.
  virtual Promise<absl::Status> TriggerWrite() = 0;

  // Returns a promise that handles the ping timeout.
  virtual Promise<absl::Status> PingTimeout() = 0;
  virtual ~PingInterface() = default;
};

// The code in this class is NOT thread safe. It has been designed to run on a
// single thread. This guarantee is achieved by spawning all the promises
// returned by this class on the same transport party.
class PingManager {
 public:
  PingManager(const ChannelArgs& channel_args, Duration ping_timeout,
              std::unique_ptr<PingInterface> ping_interface,
              std::shared_ptr<grpc_event_engine::experimental::EventEngine>
                  event_engine);

  // If there are any pending ping requests or ping acks, populates the output
  // buffer with the serialized ping frames.
  void MaybeGetSerializedPingFrames(SliceBuffer& output_buf,
                                    Duration next_allowed_ping_interval);

  // Notify the ping system that a ping has been sent. This will spawn a ping
  // timeout promise.
  void NotifyPingSent();

  // Ping Rate policy wrapper
  void ReceivedDataFrame() { ping_rate_policy_.ReceivedDataFrame(); }

  // Ping abuse policy wrapper
  bool NotifyPingAbusePolicy(const bool transport_idle) {
    return ping_abuse_policy_.ReceivedOnePing(transport_idle);
  }

  void ResetPingClock(bool is_client) {
    if (!is_client) {
      ping_abuse_policy_.ResetPingStrikes();
    }
    ping_rate_policy_.ResetPingsBeforeDataRequired();
  }

  // Ping callbacks wrapper

  // Returns a promise that resolves once a new ping is initiated and ack is
  // received for the same. The on_initiate callback is executed when the
  // ping is initiated.
  auto RequestPing(absl::AnyInvocable<void()> on_initiate, bool important) {
    return ping_callbacks_.RequestPing(std::move(on_initiate), important);
  }

  // Returns a promise that resolves once the next valid ping ack is received.
  auto WaitForPingAck() { return ping_callbacks_.WaitForPingAck(); }

  // Cancels all the callbacks for the inflight pings. This function does not
  // cancel the promises that are waiting on the ping ack.
  // This should be called as part of closing the transport to free up any
  // memory in use by the ping callbacks.
  void CancelCallbacks() { ping_callbacks_.CancelCallbacks(); }

  uint64_t StartPing() { return ping_callbacks_.StartPing(); }
  bool PingRequested() { return ping_callbacks_.PingRequested(); }
  bool ImportantPingRequested() const {
    return ping_callbacks_.ImportantPingRequested();
  }
  bool AckPing(uint64_t id) { return ping_callbacks_.AckPing(id); }
  size_t CountPingInflight() { return ping_callbacks_.CountPingInflight(); }

  Http2Frame GetHttp2PingFrame(uint64_t opaque_data) {
    return Http2PingFrame{/*ack=*/false, opaque_data};
  }

  std::optional<uint64_t> TestOnlyMaybeGetSerializedPingFrames(
      SliceBuffer& output_buffer, Duration next_allowed_ping_interval) {
    GRPC_DCHECK(!opaque_data_.has_value());
    if (NeedToPing(next_allowed_ping_interval)) {
      uint64_t opaque_data = ping_callbacks_.StartPing();
      Http2Frame frame = GetHttp2PingFrame(/*ack*/ false, opaque_data);
      Serialize(absl::Span<Http2Frame>(&frame, 1), output_buffer);
      opaque_data_ = opaque_data;
      return opaque_data;
    }

    return std::nullopt;
  }

  void AddPendingPingAck(uint64_t opaque_data);

 private:
  class PingPromiseCallbacks {
   public:
    explicit PingPromiseCallbacks(
        std::shared_ptr<grpc_event_engine::experimental::EventEngine>
            event_engine)
        : event_engine_(event_engine) {}
    Promise<absl::Status> RequestPing(absl::AnyInvocable<void()> on_initiate,
                                      bool important);
    Promise<absl::Status> WaitForPingAck();
    void CancelCallbacks() {
      important_ping_requested_ = false;
      ping_callbacks_.CancelAll(event_engine_.get());
    }
    uint64_t StartPing() {
      important_ping_requested_ = false;
      return ping_callbacks_.StartPing(SharedBitGen());
    }
    bool PingRequested() { return ping_callbacks_.ping_requested(); }
    bool ImportantPingRequested() const { return important_ping_requested_; }
    bool AckPing(uint64_t id) {
      return ping_callbacks_.AckPing(id, event_engine_.get());
    }
    size_t CountPingInflight() { return ping_callbacks_.pings_inflight(); }

    auto PingTimeout(Duration ping_timeout) {
      std::shared_ptr<InterActivityLatch<void>> latch =
          std::make_shared<InterActivityLatch<void>>();
      auto timeout_cb = [latch]() { latch->Set(); };
      std::optional<uint64_t> id = ping_callbacks_.OnPingTimeout(
          ping_timeout, event_engine_.get(), std::move(timeout_cb));

      return AssertResultType<bool>(If(
          // The scenario where OnPingTimeout returns an invalid id is when
          // the ping ack is received before spawning the ping timeout.
          // In such a case, we don't wait for the ping timeout.
          id.has_value(),
          [latch, id, ping_timeout]() {
            VLOG(2) << "Ping timeout of duration: " << ping_timeout
                    << " initiated for ping id: " << *id;
            return Map(latch->Wait(), [latch](Empty) {
              return /*trigger_ping_timeout*/ true;
            });
          },
          []() {
            // This happens if for some reason the ping ack is received before
            // the timeout timer is spawned.
            VLOG(2) << "Ping ack received. Not waiting for ping timeout.";
            return /*trigger_ping_timeout*/ false;
          }));
    }

   private:
    Chttp2PingCallbacks ping_callbacks_;
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
    // Return true if an "important" ping request needs to be started. This can
    // be used to determine if a ping should be sent as soon as possible. If
    // there are no outstanding ping requests, this is guaranteed to be false.
    // If there is at least one outstanding ping request, this may be true or
    // false.
    bool important_ping_requested_ = false;
  };

  Http2Frame GetHttp2PingFrame(bool ack, uint64_t opaque_data) {
    return Http2PingFrame{ack, opaque_data};
  }

  PingPromiseCallbacks ping_callbacks_;
  Chttp2PingAbusePolicy ping_abuse_policy_;
  Chttp2PingRatePolicy ping_rate_policy_;
  bool delayed_ping_spawned_ = false;
  std::optional<uint64_t> opaque_data_;
  std::unique_ptr<PingInterface> ping_interface_;
  std::vector<uint64_t> pending_ping_acks_;
  // Duration to wait before triggering a ping timeout.
  Duration ping_timeout_;

  void TriggerDelayedPing(Duration wait);
  bool NeedToPing(Duration next_allowed_ping_interval);
  void SpawnTimeout(Duration ping_timeout, uint64_t opaque_data);

  void SentPing() { ping_rate_policy_.SentPing(); }
};
}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_PROMISE_H
