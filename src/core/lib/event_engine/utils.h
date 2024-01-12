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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_UTILS_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_UTILS_H

#include <stdint.h>

#include <string>

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

std::string HandleToStringInternal(uintptr_t a, uintptr_t b);

// Returns a string representation of the EventEngine::*Handle types
template <typename Handle>
std::string HandleToString(const Handle& handle) {
  return HandleToStringInternal(handle.keys[0], handle.keys[1]);
}

grpc_core::Timestamp ToTimestamp(grpc_core::Timestamp now,
                                 EventEngine::Duration delta);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_UTILS_H
