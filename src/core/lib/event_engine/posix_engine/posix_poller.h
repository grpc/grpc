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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_POLLER_H
#define GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_POLLER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/poller.h"

namespace grpc_event_engine {
namespace posix_engine {

// Interface for posix pollers
class PosixPoller : public experimental::Poller {
 public:
  virtual ~Poller() = default;
  virtual absl::Status Work(grpc_core::Duration timeout) = 0;
  virtual void Kick() = 0;
  virtual void Shutdown() = 0;

  // Return an opaque handle to perform actions on the provided file descriptor.
  virtual EventHandle* CreateHandle(int fd, absl::string_view name,
                                    bool track_err) = 0;
}

}  // namespace posix_engine
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_POLLER_H