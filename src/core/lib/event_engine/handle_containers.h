// Copyright 2022 The gRPC Authors
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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_HANDLE_CONTAINERS_H
#define GRPC_CORE_LIB_EVENT_ENGINE_HANDLE_CONTAINERS_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <cstdint>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"

#include <grpc/event_engine/event_engine.h>

namespace grpc_event_engine {
namespace experimental {

// Used for heterogeneous lookup of TaskHandles in abseil containers.
template <typename TaskHandle>
struct TaskHandleComparator {
  struct Hash {
    using HashType = std::pair<const intptr_t, const intptr_t>;
    using is_transparent = void;
    size_t operator()(const TaskHandle& handle) const {
      return absl::Hash<HashType>()({handle.keys[0], handle.keys[1]});
    }
  };
  struct Eq {
    using is_transparent = void;
    bool operator()(const TaskHandle& lhs, const TaskHandle& rhs) const {
      return lhs.keys[0] == rhs.keys[0] && lhs.keys[1] == rhs.keys[1];
    }
  };
};

using TaskHandleSet = absl::flat_hash_set<
    grpc_event_engine::experimental::EventEngine::TaskHandle,
    TaskHandleComparator<
        grpc_event_engine::experimental::EventEngine::TaskHandle>::Hash,
    TaskHandleComparator<
        grpc_event_engine::experimental::EventEngine::TaskHandle>::Eq>;

using LookupTaskHandleSet = absl::flat_hash_set<
    grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTaskHandle,
    TaskHandleComparator<grpc_event_engine::experimental::EventEngine::
                             DNSResolver::LookupTaskHandle>::Hash,
    TaskHandleComparator<grpc_event_engine::experimental::EventEngine::
                             DNSResolver::LookupTaskHandle>::Eq>;

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_HANDLE_CONTAINERS_H
