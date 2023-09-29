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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_CALLBACKS_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_CALLBACKS_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/hash/hash.h"
#include "absl/random/bit_gen_ref.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/gprpp/time.h"

namespace grpc_core {

class Chttp2PingCallbacks {
 public:
  using Callback = absl::AnyInvocable<void()>;

  void RequestPing() { ping_requested_ = true; }
  void OnPing(Callback on_start, Callback on_ack);
  void OnPingAck(Callback on_ack);

  GRPC_MUST_USE_RESULT uint64_t
  StartPing(absl::BitGenRef bitgen, Duration ping_timeout, Callback on_timeout,
            grpc_event_engine::experimental::EventEngine* event_engine);
  bool AckPing(uint64_t id,
               grpc_event_engine::experimental::EventEngine* event_engine);

  void CancelAll(grpc_event_engine::experimental::EventEngine* event_engine);

  bool ping_requested() const { return ping_requested_; }
  size_t pings_inflight() const { return inflight_.size(); }

 private:
  using CallbackVec = std::vector<Callback>;
  struct InflightPing {
    grpc_event_engine::experimental::EventEngine::TaskHandle on_timeout;
    CallbackVec on_ack;
  };
  absl::flat_hash_map<uint64_t, InflightPing> inflight_;
  uint64_t most_recent_inflight_ = 0;
  bool ping_requested_ = false;
  CallbackVec on_start_;
  CallbackVec on_ack_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_CALLBACKS_H
