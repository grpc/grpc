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
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace http2 {

// KeepAlive
class KeepAliveSystemInterface {
 public:
  // Returns a promise that sends a ping frame and resolves when the ack is
  // received.
  virtual Promise<absl::Status> SendPing() = 0;

  // Returns a promise that processes the keepalive timeout.
  virtual Promise<absl::Status> KeepAliveTimeout() = 0;

  // Returns true if a keepalive ping needs to be sent.
  virtual bool NeedToSendKeepAlivePing() = 0;
  virtual ~KeepAliveSystemInterface() = default;
};

class KeepAliveSystem {
 public:
  KeepAliveSystem(
      std::unique_ptr<KeepAliveSystemInterface> keep_alive_interface,
      Duration keepalive_timeout, Duration keepalive_interval);
  void Spawn(Party* party);

  // Needs to be called when any data is read from the peer.
  void GotData() { ReceivedData(); }
  void SetKeepAliveTimeout(Duration keepalive_timeout) {
    keepalive_timeout_ = keepalive_timeout;
  }

 private:
  // Returns a promise that sleeps for the keepalive timeout and triggers the
  // keepalive timeout based on whether some data is read within the keepalive
  // timeout.
  auto WaitForKeepAliveTimeout();

  // Returns a promise that sends a keepalive ping and spawns the keepalive
  // timeout promise. The promise resolves in the following scenarios:
  // 1. Ping ack is received within the keepalive timeout (when the ping ack is
  //     received, a call to GotData() is expected).
  // 2. Ping ack is received after keepalive timeout (but before the ping
  //     timeout). In this case, if there is some data received while
  //     waiting for ping ack, the keepalive timeout is not triggered and the
  //     promise resolves when SendPing() resolves.
  // 3. No data is received within the keepalive timeout and the keepalive
  //    timeout is triggered.
  auto TimeoutAndSendPing();

  // Returns a promise that determines if a keepalive ping needs to be sent and
  // sends a keepalive ping if needed. The promise resolves when either a ping
  // ack is received or the keepalive timeout is triggered.
  auto MaybeSendKeepAlivePing();

  auto WaitForData() {
    return [this]() -> Poll<absl::Status> {
      if (IsDataReceivedInLastCycle()) {
        VLOG(2) << "WaitForData: Data received. Poll resolved";
        return absl::OkStatus();
      } else {
        VLOG(2) << "WaitForData: Data not received. Poll pending";
        waker_ = GetContext<Activity>()->MakeNonOwningWaker();
        return Pending{};
      }
    };
  }
  // Needs to be called when data is read from the peer.
  void ReceivedData() {
    if (IsKeepAliveTimeoutTriggered()) {
      VLOG(2)
          << "KeepAlive timeout triggered. Not setting data_received_ to true";
      return;
    }
    VLOG(2) << "Data received. Setting data_received_ to true";
    state_ |= kDataReceivedInLastCycle;
    auto waker = std::move(waker_);
    waker.Wakeup();
  }
  auto SendPing() {
    DCHECK(!IsDataReceivedInLastCycle());
    return keep_alive_interface_->SendPing();
  }

  // If no data is received in the last keepalive_interval, we should send a
  // keepalive ping. This also means that there can be scenarios where we would
  // send one keepalive ping in ~(2*keepalive_interval).
  bool NeedToSendKeepAlivePing() {
    return (IsDataReceivedInLastCycle() == false) &&
           (keep_alive_interface_->NeedToSendKeepAlivePing());
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool IsKeepAliveTimeoutTriggered()
      const {
    return (state_ & kKeepAliveTimeoutTriggered);
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool IsDataReceivedInLastCycle() const {
    return (state_ & kDataReceivedInLastCycle);
  }

  std::unique_ptr<KeepAliveSystemInterface> keep_alive_interface_;
  // bit to indicate if data is received in the last cycle
  static constexpr uint8_t kDataReceivedInLastCycle = 0;
  // bit to indicate if the keepalive timeout is triggered
  static constexpr uint8_t kKeepAliveTimeoutTriggered = 1;
  // If the keepalive_timeout_ is set to infinity, then the timeout is dictated
  // by the ping timeout. Otherwise, this field can be used to set a specific
  // timeout for keepalive pings.
  Duration keepalive_timeout_;
  Duration keepalive_interval_;
  uint8_t state_ = 0;
  Waker waker_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_PROMISE_H
