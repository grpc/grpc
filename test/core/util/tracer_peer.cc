/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "test/core/util/test_config.h"

#include "src/core/lib/debug/trace.h"

namespace grpc_core {

// This class is a friend of TraceFlag, and can be used to manually turn on
// certain tracers for tests.
class TraceFlagPeer {
 public:
  TraceFlagPeer(TraceFlag* flag) : flag_(flag) {}
  void enable() { flag_->set_enabled(1); }
  void disable() { flag_->set_enabled(0); }

 private:
  TraceFlag* flag_;
};
}  // namespace grpc_core

void grpc_tracer_peer_enable_flag(grpc_core::TraceFlag* flag) {
  grpc_core::TraceFlagPeer peer(flag);
  peer.enable();
}
