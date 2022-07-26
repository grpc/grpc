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

#include "src/core/lib/event_engine/utils.h"

#include <stdint.h>

#include <algorithm>
#include <chrono>

#include "absl/strings/str_cat.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

std::string HandleToString(EventEngine::TaskHandle handle) {
  return absl::StrCat("{", handle.keys[0], ",", handle.keys[1], "}");
}

grpc_core::Timestamp ToTimestamp(grpc_core::Timestamp now,
                                 EventEngine::Duration delta) {
  return now +
         std::max(grpc_core::Duration::Milliseconds(1),
                  grpc_core::Duration::NanosecondsRoundUp(delta.count())) +
         grpc_core::Duration::Milliseconds(1);
}

size_t Milliseconds(EventEngine::Duration d) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

}  // namespace experimental
}  // namespace grpc_event_engine
