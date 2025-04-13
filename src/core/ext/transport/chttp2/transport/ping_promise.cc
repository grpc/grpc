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

namespace grpc_core {
namespace http2 {
KeepAliveSystem::KeepAliveSystem(
    std::unique_ptr<KeepAliveSystemInterface> keep_alive_interface,
    Duration keepalive_timeout)
    : keep_alive_interface_(std::move(keep_alive_interface)),
      keepalive_timeout_(keepalive_timeout) {}

void KeepAliveSystem::Spawn(Party* party, Duration keepalive_interval) {
  party->Spawn(
      "KeepAlive", Loop([this, keepalive_interval]() {
        return TrySeq(Race(WaitForData(),
                           TrySeq(Sleep(keepalive_interval),
                                  If(keepalive_timeout_ != Duration::Infinity(),
                                     TimeoutAndSendPing(),
                                     [this] { return SendPing(); }))),
                      []() -> LoopCtl<absl::Status> { return Continue(); });
      }),
      [](auto status) { VLOG(2) << "KeepAlive end with status: " << status; });
}
}  // namespace http2
}  // namespace grpc_core
