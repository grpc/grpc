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

#include "src/core/lib/event_engine/shim.h"

#include <grpc/support/port_platform.h>

#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/port.h"

namespace grpc_event_engine::experimental {

bool UseEventEngineClient() {
#if defined(GRPC_DO_NOT_INSTANTIATE_POSIX_POLLER)
  return false;
#endif
  return grpc_core::IsEventEngineClientEnabled();
}

bool UseEventEngineListener() {
#if defined(GRPC_DO_NOT_INSTANTIATE_POSIX_POLLER)
  return false;
#endif
  return grpc_core::IsEventEngineListenerEnabled();
}

bool UsePollsetAlternative() {
  return UseEventEngineClient() && UseEventEngineListener() &&
         grpc_core::IsPollsetAlternativeEnabled();
}

}  // namespace grpc_event_engine::experimental
