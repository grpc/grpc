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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_WAKEUP_FD_POSIX_DEFAULT_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_WAKEUP_FD_POSIX_DEFAULT_H
#include <grpc/support/port_platform.h>

#include <memory>

#include "absl/status/statusor.h"

namespace grpc_event_engine {
namespace experimental {

class WakeupFd;

// Returns true if wakeup-fd is supported by the system.
bool SupportsWakeupFd();

// Create and return an initialized WakeupFd instance if supported.
absl::StatusOr<std::unique_ptr<WakeupFd>> CreateWakeupFd();

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_WAKEUP_FD_POSIX_DEFAULT_H
