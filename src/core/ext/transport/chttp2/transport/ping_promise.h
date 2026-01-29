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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/ping_abuse_policy.h"
#include "src/core/ext/transport/chttp2/transport/ping_callbacks.h"
#include "src/core/ext/transport/chttp2/transport/ping_rate_policy.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/time.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace http2 {

#define GRPC_HTTP2_PING_LOG                                \
  LOG_IF(INFO, (GRPC_TRACE_FLAG_ENABLED(http) ||           \
                GRPC_TRACE_FLAG_ENABLED(bdp_estimator) ||  \
                GRPC_TRACE_FLAG_ENABLED(http_keepalive) || \
                GRPC_TRACE_FLAG_ENABLED(http2_ping)))

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
 private:
  struct TriggerPingArgs {
    TriggerPingArgs(std::optional<Duration> delayed_ping_wait,
                    bool need_to_ping)
        : delayed_ping_wait(delayed_ping_wait), need_to_ping(need_to_ping) {
      GRPC_DCHECK(!(delayed_ping_wait.has_value() && need_to_ping));
    }

    std::optional<Duration> delayed_ping_wait;
    bool need_to_ping;
  };
  class PingPromiseCallbacks {
   public:
    explicit PingPromiseCallbacks(
        std::shared_ptr<grpc_event_engine::experimental::EventEngine>
            event_engine)
        : event_engine_(event_engine) {}
    ~PingPromiseCallbacks() { CancelCallbacks(); }
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

  TriggerPingArgs NeedToPing(Duration next_allowed_ping_interval);

  void SentPing() { ping_rate_policy_.SentPing(); }

 public:
  PingManager(const ChannelArgs& channel_args, Duration ping_timeout,
              std::unique_ptr<PingInterface> ping_interface,
              std::shared_ptr<grpc_event_engine::experimental::EventEngine>
                  event_engine);

  // If there are any pending ping requests or ping acks, populates the output
  // buffer with the serialized ping frames. Returns the arguments for
  // scheduling the delayed ping.
  std::optional<Duration> MaybeGetSerializedPingFrames(
      SliceBuffer& output_buf, Duration next_allowed_ping_interval);

  // Notify the ping system that a ping has been sent. Returns the opaque data
  // of the ping frame if a new ping was sent. The caller is expected to
  // spawn a ping timeout promise using TimeoutPromise() for this returned
  // value.
  std::optional<uint64_t> NotifyPingSent();

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

  void AddPendingPingAck(uint64_t opaque_data);

  auto TimeoutPromise(const uint64_t opaque_data) {
    return AssertResultType<absl::Status>(Race(
        TrySeq(ping_callbacks_.PingTimeout(ping_timeout_),
               [this, opaque_data](bool trigger_ping_timeout) mutable {
                 return If(
                     trigger_ping_timeout,
                     [this, opaque_data]() {
                       GRPC_HTTP2_PING_LOG
                           << " Ping ack not received for id=" << opaque_data
                           << ". Ping timeout triggered.";
                       return ping_interface_->PingTimeout();
                     },
                     []() { return absl::OkStatus(); });
               }),
        ping_callbacks_.WaitForPingAck()));
  };

  auto DelayedPingPromise(const Duration wait) {
    return TrySeq(
        Sleep(wait),
        [this]() mutable { return ping_interface_->TriggerWrite(); },
        [this]() {
          delayed_ping_spawned_ = false;
          return absl::OkStatus();
        });
  }
};
}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_PROMISE_H
