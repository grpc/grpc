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

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace http2 {

// KeepAlive
class KeepAliveSystemInterface {
 public:
  virtual Promise<absl::Status> SendPing() = 0;
  virtual Promise<absl::Status> KeepAliveTimeout() = 0;
  virtual ~KeepAliveSystemInterface() = default;
};

class KeepAliveSystem {
 private:
  struct NextKeepAlive {
    Duration wait;
    bool send_ping;
  };
  auto SendPing() { return keep_alive_interface_->SendPing(); }
  // Will be called if keepalive_timeout_ is not infinity.
  auto TimeoutAndSendPing() {
    return Race(
        TrySeq(
            Sleep(keepalive_timeout_),
            [this] { return keep_alive_interface_->KeepAliveTimeout(); },
            [] { return absl::CancelledError("KeepAlive timeout"); }),
        SendPing());
  }
  NextKeepAlive NeedToSendKeepAlivePing() {
    Timestamp next_allowed_time =
        last_data_received_time_ + keepalive_interval_;
    Timestamp now = Timestamp::Now();
    bool send_ping = next_allowed_time <= now;
    return (send_ping) ? NextKeepAlive{Duration::Zero(), true}
                       : NextKeepAlive{next_allowed_time - now, false};
  }
  void UpdateNextKeepAliveInterval(NextKeepAlive next_ping) {
    if (next_ping.send_ping) {
      next_keepalive_interval_ = keepalive_interval_;
    } else {
      next_keepalive_interval_ =
          std::max(next_ping.wait, min_keepalive_interval_);
    }
  }
  auto MaybeSendKeepAlivePing() {
    auto next_ping = NeedToSendKeepAlivePing();
    return TrySeq(If(
                      next_ping.send_ping,
                      [this]() {
                        // TODO(akshitpatel) : [PH2][P0] : Should we wait for
                        // ping ack if some data is received after the ping is
                        // sent?
                        return If(keepalive_timeout_ != Duration::Infinity(),
                                  TimeoutAndSendPing(),
                                  [this] { return SendPing(); });
                      },
                      []() { return Immediate(absl::OkStatus()); }),
                  [this, next_ping] {
                    UpdateNextKeepAliveInterval(next_ping);
                    return Immediate(absl::OkStatus());
                  });
  }
  std::unique_ptr<KeepAliveSystemInterface> keep_alive_interface_;
  // If the keepalive_timeout_ is set to infinity, then the timeout is dictated
  // by the ping timeout. Otherwise, the transport can choose to set a specific
  // timeout for keepalive pings using this field.
  Duration keepalive_timeout_;
  Duration keepalive_interval_;
  Duration next_keepalive_interval_;
  Timestamp last_data_received_time_;
  // TODO(akshitpatel) : [PH2][P0] : Figure out if this is good.
  Duration min_keepalive_interval_ = Duration::Seconds(5);

 public:
  KeepAliveSystem(
      std::unique_ptr<KeepAliveSystemInterface> keep_alive_interface,
      Duration keepalive_timeout, Duration keepalive_interval);
  void Spawn(Party* party);
  void GotData() { last_data_received_time_ = Timestamp::Now(); }
  void SetKeepAliveTimeout(Duration keepalive_timeout) {
    keepalive_timeout_ = keepalive_timeout;
  }
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_PROMISE_H
