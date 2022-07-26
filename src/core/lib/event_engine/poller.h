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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_POLLER_H
#define GRPC_CORE_LIB_EVENT_ENGINE_POLLER_H

#include <grpc/support/port_platform.h>

#include "absl/container/inlined_vector.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>

namespace grpc_event_engine {
namespace experimental {

// A generic cross-platform "poller" concept.
// Concrete implementations will likely manage a set of sockets/file
// descriptors/etc, allowing threads to drive polling and event processing via
// Work(...).
class Poller {
 public:
  // This initial vector size may need to be tuned
  using Events = absl::InlinedVector<EventEngine::Closure*, 5>;
  struct DeadlineExceeded {};
  struct Kicked {};
  using WorkResult = absl::variant<Events, DeadlineExceeded, Kicked>;

  virtual ~Poller() = default;
  // Poll once for events, returning a collection of Closures to be executed.
  //
  // Returns:
  //  * absl::AbortedError if it was Kicked.
  //  * absl::DeadlineExceeded if timeout occurred
  //  * A collection of closures to execute, otherwise
  virtual WorkResult Work(EventEngine::Duration timeout) = 0;
  // Trigger the threads executing Work(..) to break out as soon as possible.
  virtual void Kick() = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_POLLER_H
