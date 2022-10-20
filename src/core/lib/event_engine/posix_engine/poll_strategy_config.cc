// Copyright 2022 gRPC authors.
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

#include "src/core/lib/event_engine/posix_engine/poll_strategy_config.h"

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP
#include "src/core/lib/gprpp/global_config.h"

GPR_GLOBAL_CONFIG_DEFINE_STRING(
    grpc_poll_strategy, "all",
    "Declares which polling engines to try when starting gRPC. "
    "This is a comma-separated list of engines, which are tried in priority "
    "order first -> last.");

namespace grpc_event_engine {
namespace posix_engine {
const char* PollStrategy() {
  static const char* poll_strategy =
      GPR_GLOBAL_CONFIG_GET(grpc_poll_strategy).release();
  return poll_strategy;
}
}  // namespace posix_engine
}  // namespace grpc_event_engine

#endif
