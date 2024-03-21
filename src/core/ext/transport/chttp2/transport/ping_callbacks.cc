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

#include <utility>

#include "absl/meta/type_traits.h"
#include "absl/random/distributions.h"

#include <grpc/support/log.h>

grpc_core::TraceFlag grpc_ping_trace(false, "http2_ping");

namespace grpc_core {

void Chttp2PingCallbacks::OnPing(Callback on_start, Callback on_ack) {
  on_start_.emplace_back(std::move(on_start));
  on_ack_.emplace_back(std::move(on_ack));
  ping_requested_ = true;
}

void Chttp2PingCallbacks::OnPingAck(Callback on_ack) {
  auto it = inflight_.find(most_recent_inflight_);
  if (it != inflight_.end()) {
    it->second.on_ack.emplace_back(std::move(on_ack));
    return;
  }
  ping_requested_ = true;
  on_ack_.emplace_back(std::move(on_ack));
}

uint64_t Chttp2PingCallbacks::StartPing(absl::BitGenRef bitgen) {
  uint64_t id;
  do {
    id = absl::Uniform<uint64_t>(bitgen);
  } while (inflight_.contains(id));
  CallbackVec cbs = std::move(on_start_);
  CallbackVec().swap(on_start_);
  InflightPing inflight;
  inflight.on_ack.swap(on_ack_);
  started_new_ping_without_setting_timeout_ = true;
  inflight_.emplace(id, std::move(inflight));
  most_recent_inflight_ = id;
  ping_requested_ = false;
  for (auto& cb : cbs) {
    cb();
  }
  return id;
}

bool Chttp2PingCallbacks::AckPing(
    uint64_t id, grpc_event_engine::experimental::EventEngine* event_engine) {
  auto ping = inflight_.extract(id);
  if (ping.empty()) return false;
  if (ping.mapped().on_timeout !=
      grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid) {
    event_engine->Cancel(ping.mapped().on_timeout);
  }
  for (auto& cb : ping.mapped().on_ack) {
    cb();
  }
  return true;
}

void Chttp2PingCallbacks::CancelAll(
    grpc_event_engine::experimental::EventEngine* event_engine) {
  CallbackVec().swap(on_start_);
  CallbackVec().swap(on_ack_);
  for (auto& cbs : inflight_) {
    CallbackVec().swap(cbs.second.on_ack);
    if (cbs.second.on_timeout !=
        grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid) {
      event_engine->Cancel(std::exchange(
          cbs.second.on_timeout,
          grpc_event_engine::experimental::EventEngine::TaskHandle::kInvalid));
    }
  }
  ping_requested_ = false;
}

absl::optional<uint64_t> Chttp2PingCallbacks::OnPingTimeout(
    Duration ping_timeout,
    grpc_event_engine::experimental::EventEngine* event_engine,
    Callback callback) {
  GPR_ASSERT(started_new_ping_without_setting_timeout_);
  started_new_ping_without_setting_timeout_ = false;
  auto it = inflight_.find(most_recent_inflight_);
  if (it == inflight_.end()) return absl::nullopt;
  it->second.on_timeout =
      event_engine->RunAfter(ping_timeout, std::move(callback));
  return most_recent_inflight_;
}

}  // namespace grpc_core
