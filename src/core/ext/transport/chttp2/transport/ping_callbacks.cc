// Copyright 2023 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/ping_callbacks.h"

#include <cstdint>

#include "absl/random/distributions.h"

namespace grpc_core {

void Chttp2PingCallbacks::OnPing(Callback on_start, Callback on_ack) {
  on_start_.emplace_back(std::move(on_start));
  on_ack_.emplace_back(std::move(on_ack));
  ping_requested_ = true;
}

void Chttp2PingCallbacks::OnPingAck(Callback on_ack) {
  auto it = inflight_.find(most_recent_inflight_);
  if (it != inflight_.end()) {
    it->second.emplace_back(std::move(on_ack));
    return;
  }
  ping_requested_ = true;
  on_ack_.emplace_back(on_ack);
}

uint64_t Chttp2PingCallbacks::StartPing(absl::BitGenRef bitgen) {
  uint64_t id;
  do {
    id = absl::Uniform<uint64_t>(bitgen);
  } while (inflight_.contains(id));
  CallbackVec cbs = std::move(on_start_);
  inflight_.emplace(id, std::move(on_ack_));
  CallbackVec().swap(on_start_);
  CallbackVec().swap(on_ack_);
  most_recent_inflight_ = id;
  ping_requested_ = false;
  for (auto& cb : cbs) {
    cb();
  }
  return id;
}

bool Chttp2PingCallbacks::AckPing(uint64_t id) {
  auto ping = inflight_.extract(id);
  if (ping.empty()) return false;
  for (auto& cb : ping.mapped()) {
    cb();
  }
  return true;
}

void Chttp2PingCallbacks::CancelAll() {
  CallbackVec().swap(on_start_);
  CallbackVec().swap(on_ack_);
  for (auto& cbs : inflight_) {
    CallbackVec().swap(cbs.second);
  }
}

}  // namespace grpc_core
