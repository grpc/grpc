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
#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_KEEPALIVE_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_KEEPALIVE_H

#include "absl/status/status.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/promise.h"

namespace grpc_core {
namespace http2 {

#define KEEPALIVE_LOG VLOG(2)

class KeepAliveInterface {
 public:
  // Returns a promise that sends a ping frame and resolves when the ack is
  // received.
  virtual Promise<absl::Status> SendPingAndWaitForAck() = 0;

  // Returns a promise that processes the keepalive timeout.
  virtual Promise<absl::Status> OnKeepAliveTimeout() = 0;

  // Returns true if a keepalive ping needs to be sent.
  virtual bool NeedToSendKeepAlivePing() = 0;
  virtual ~KeepAliveInterface() = default;
};

class KeepaliveManager {
 public:
  KeepaliveManager(std::unique_ptr<KeepAliveInterface> keep_alive_interface,
                   Duration keepalive_timeout, Duration keepalive_interval);
  void Spawn(Party* party);

  // Needs to be called when any data is read from the endpoint.
  void GotData() {
    if (keep_alive_timeout_triggered_) {
      KEEPALIVE_LOG
          << "KeepAlive timeout triggered. Not setting data_received_ to true";
      return;
    }
    KEEPALIVE_LOG << "Data received. Setting data_received_ to true";
    data_received_in_last_cycle_ = true;
    auto waker = std::move(waker_);
    // This will only trigger a wakeup if WaitForData() is pending on this
    // waker. Otherwise this would be noop.
    waker.Wakeup();
  }
  void SetKeepAliveTimeout(Duration keepalive_timeout) {
    keepalive_timeout_ = keepalive_timeout;
  }

 private:
  // Returns a promise that sleeps for the keepalive_timeout_ and triggers the
  // keepalive timeout unless data is read within the keepalive timeout.
  auto WaitForKeepAliveTimeout();

  // Returns a promise that sends a keepalive ping and spawns the keepalive
  // timeout promise. The promise resolves in the following scenarios:
  // 1. Ping ack is received within the keepalive timeout (when the ping ack is
  //     received, a call to GotData() is expected).
  // 2. Ping ack is received after keepalive timeout (but before the ping
  //     timeout). In this case, if there is some data received while
  //     waiting for ping ack, the keepalive timeout is not triggered and the
  //     promise resolves when SendPingAndWaitForAck() resolves.
  // 3. No data is received within the keepalive timeout and the keepalive
  //    timeout is triggered.
  auto TimeoutAndSendPing();

  // Returns a promise that determines if a keepalive ping needs to be sent and
  // sends a keepalive ping if needed. The promise resolves when either a ping
  // ack is received or the keepalive timeout is triggered.
  auto MaybeSendKeepAlivePing();

  auto WaitForData() {
    return [this]() -> Poll<absl::Status> {
      if (data_received_in_last_cycle_) {
        KEEPALIVE_LOG << "WaitForData: Data received. Poll resolved";
        return absl::OkStatus();
      } else {
        KEEPALIVE_LOG << "WaitForData: Data not received. Poll pending";
        waker_ = GetContext<Activity>()->MakeNonOwningWaker();
        return Pending{};
      }
    };
  }
  auto SendPingAndWaitForAck() {
    DCHECK_EQ(data_received_in_last_cycle_, false);
    return keep_alive_interface_->SendPingAndWaitForAck();
  }

  // If no data is received in the last keepalive_interval, we should send a
  // keepalive ping. This also means that there can be scenarios where we would
  // send one keepalive ping in ~(2*keepalive_interval).
  bool NeedToSendKeepAlivePing() {
    return (!data_received_in_last_cycle_) &&
           (keep_alive_interface_->NeedToSendKeepAlivePing());
  }

  std::unique_ptr<KeepAliveInterface> keep_alive_interface_;
  // If the keepalive_timeout_ is set to infinity, then the timeout is dictated
  // by the ping timeout. Otherwise, this field can be used to set a specific
  // timeout for keepalive pings.
  Duration keepalive_timeout_;
  Duration keepalive_interval_;
  bool data_received_in_last_cycle_ = false;
  bool keep_alive_timeout_triggered_ = false;
  Waker waker_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_KEEPALIVE_H
