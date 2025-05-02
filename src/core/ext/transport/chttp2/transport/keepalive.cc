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
#include "src/core/ext/transport/chttp2/transport/keepalive.h"

#include "src/core/lib/promise/all_ok.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"

namespace grpc_core {
namespace http2 {
KeepaliveManager::KeepaliveManager(
    std::unique_ptr<KeepAliveInterface> keep_alive_interface,
    Duration keepalive_timeout, Duration keepalive_interval)
    : keep_alive_interface_(std::move(keep_alive_interface)),
      keepalive_timeout_(keepalive_timeout),
      keepalive_interval_(keepalive_interval) {}

auto KeepaliveManager::WaitForKeepAliveTimeout() {
  return AssertResultType<absl::Status>(
      TrySeq(Sleep(keepalive_timeout_), [this] {
        return If(
            data_received_in_last_cycle_,
            [] {
              KEEPALIVE_LOG << "Keepalive timeout triggered but "
                            << "received data. Resolving with ok status";
              return Immediate(absl::OkStatus());
            },
            [this] {
              KEEPALIVE_LOG << "Keepalive timeout triggered and no "
                               "data received. Triggering keepalive timeout.";
              // Once the keepalive timeout is triggered, ensure that
              // WaitForData() is never resolved. This is needed as the
              // keepalive loop should break once the timeout is triggered.
              keep_alive_timeout_triggered_ = true;
              return TrySeq(keep_alive_interface_->OnKeepAliveTimeout(), [] {
                return absl::CancelledError("keepalive timeout");
              });
            });
      }));
}
auto KeepaliveManager::TimeoutAndSendPing() {
  DCHECK(!data_received_in_last_cycle_);
  DCHECK(keepalive_timeout_ != Duration::Infinity());

  return AllOk<absl::Status>(Race(WaitForData(), WaitForKeepAliveTimeout()),
                             SendPingAndWaitForAck());
}
auto KeepaliveManager::MaybeSendKeepAlivePing() {
  LOG(INFO) << "KeepaliveManager::MaybeSendKeepAlivePing";
  return AssertResultType<absl::Status>(
      TrySeq(If(
                 NeedToSendKeepAlivePing(),
                 [this]() {
                   return If(
                       keepalive_timeout_ != Duration::Infinity(),
                       [this] { return TimeoutAndSendPing(); },
                       [this] { return SendPingAndWaitForAck(); });
                 },
                 []() { return Immediate(absl::OkStatus()); }),
             [this] {
               data_received_in_last_cycle_ = false;
               return Immediate(absl::OkStatus());
             }));
}

void KeepaliveManager::Spawn(Party* party) {
  party->Spawn("KeepAliveLoop", Loop([this]() {
                 return TrySeq(
                     Sleep(keepalive_interval_),
                     [this]() { return MaybeSendKeepAlivePing(); },
                     []() -> LoopCtl<absl::Status> { return Continue(); });
               }),
               [](auto status) {
                 LOG(INFO) << "KeepAlive end with status: " << status;
               });
}
}  // namespace http2
}  // namespace grpc_core
