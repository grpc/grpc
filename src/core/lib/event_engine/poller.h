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

#include "src/core/lib/gprpp/time.h"

namespace grpc_event_engine {
namespace experimental {

// A generic cross-platform "poller" concept.
// Concrete implementations will likely manage a set of sockets/file
// descriptors/etc, allowing threads to drive polling and event processing via
// Work(...).
class Poller {
 public:
  virtual ~Poller() = default;
  // Poll for events, executing or dispatching them as appropriate.
  //
  // Returns:
  //  * absl::AbortedError if it was Kicked.
  //  * absl::DeadlineExceeded if timeout occurred
  //  * absl::OkStatus otherwise
  virtual absl::Status Work(grpc_core::Duration timeout) = 0;
  // Trigger the threads executing Work(..) to break out as soon as possible.
  virtual void Kick() = 0;
  // Shut down the poller.
  // There must be no threads calling Work(...).
  virtual void Shutdown() = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_POLLER_H