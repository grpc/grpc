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

#include "absl/functional/function_ref.h"

#include <grpc/event_engine/event_engine.h>

namespace grpc_event_engine {
namespace experimental {

// A generic cross-platform "poller" concept.
// Concrete implementations will likely manage a set of sockets/file
// descriptors/etc, allowing threads to drive polling and event processing via
// Work(...).
class Poller {
 public:
  enum class WorkResult { kOk, kDeadlineExceeded, kKicked };

  virtual ~Poller() = default;
  // Poll once for events and process received events. The callback function
  // "schedule_poll_again" is expected to be run synchronously prior to
  // processing received events. The callback's responsibility primarily is to
  // schedule Poller::Work asynchronously again. This would ensure that the next
  // polling cycle would run as quickly as possible to ensure continuous
  // polling.
  //
  // Returns:
  //  * Poller::WorkResult::kKicked if it was Kicked.
  //  * Poller::WorkResult::kDeadlineExceeded if timeout occurred
  //  * Poller::WorkResult::kOk, otherwise indicating that the callback function
  //  was run synchonously before some events were processed.
  virtual WorkResult Work(EventEngine::Duration timeout,
                          absl::FunctionRef<void()> schedule_poll_again) = 0;
  // Trigger the threads executing Work(..) to break out as soon as possible.
  virtual void Kick() = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_POLLER_H
