// Copyright 2023 The gRPC Authors
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

#include <stdint.h>

#include <grpc/event_engine/event_engine.h>

namespace grpc_event_engine {
namespace experimental {

constexpr EventEngine::TaskHandle EventEngine::TaskHandle::kInvalid = {-1, -1};
constexpr EventEngine::ConnectionHandle
    EventEngine::ConnectionHandle::kInvalid = {-1, -1};
constexpr EventEngine::DNSResolver::LookupTaskHandle
    EventEngine::DNSResolver::LookupTaskHandle::kInvalid = {-1, -1};

bool operator==(const EventEngine::TaskHandle& lhs,
                const EventEngine::TaskHandle& rhs) {
  return lhs.keys[0] == rhs.keys[0] && lhs.keys[1] == rhs.keys[1];
}

bool operator!=(const EventEngine::TaskHandle& lhs,
                const EventEngine::TaskHandle& rhs) {
  return !(lhs == rhs);
}

bool operator==(const EventEngine::ConnectionHandle& lhs,
                const EventEngine::ConnectionHandle& rhs) {
  return lhs.keys[0] == rhs.keys[0] && lhs.keys[1] == rhs.keys[1];
}

bool operator!=(const EventEngine::ConnectionHandle& lhs,
                const EventEngine::ConnectionHandle& rhs) {
  return !(lhs == rhs);
}

bool operator==(const EventEngine::DNSResolver::LookupTaskHandle& lhs,
                const EventEngine::DNSResolver::LookupTaskHandle& rhs) {
  return lhs.keys[0] == rhs.keys[0] && lhs.keys[1] == rhs.keys[1];
}

bool operator!=(const EventEngine::DNSResolver::LookupTaskHandle& lhs,
                const EventEngine::DNSResolver::LookupTaskHandle& rhs) {
  return !(lhs == rhs);
}

}  // namespace experimental
}  // namespace grpc_event_engine
