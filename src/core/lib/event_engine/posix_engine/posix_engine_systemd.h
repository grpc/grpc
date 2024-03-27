// Copyright 2022 gRPC Authors
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
#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_SYSTEMD_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_SYSTEMD_H

#include <grpc/event_engine/event_engine.h>

namespace grpc_event_engine {
namespace experimental {

// Checks if a particular file descriptor is part of the file
// descriptors provided by systemd when the process is started
// through socket activation
absl::StatusOr<bool> IsSystemdPreallocatedFd(int fd);

// Same as above, but log and swallows the errors.
// Useful in contexts where status cannot bubble up.
// Fallbacks to false, to consider the FD is *not* preallocated
// and thus will attempt to be managed in the normal workflow
bool IsSystemdPreallocatedFdOrLogErrorsWithFalseFallback(int fd);

// Checks the provided address matches the information of one
// of the file descriptors provided by systemd when the process
// is started through socket activation.
absl::StatusOr<absl::optional<int>> MaybeGetSystemdPreallocatedFdFromAddr(
    const EventEngine::ResolvedAddress& addr);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_SYSTEMD_H