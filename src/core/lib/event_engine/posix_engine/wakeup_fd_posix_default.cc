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
#include <grpc/support/port_platform.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_eventfd.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_pipe.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix.h"
#include "src/core/lib/iomgr/port.h"

namespace grpc_event_engine::experimental {

#ifdef GRPC_POSIX_WAKEUP_FD

absl::StatusOr<std::unique_ptr<WakeupFd>> NotSupported() {
  return absl::NotFoundError("Wakeup-fd is not supported on this system");
}

namespace {
absl::StatusOr<std::unique_ptr<WakeupFd>> (*g_wakeup_fd_fn)() =
    []() -> absl::StatusOr<std::unique_ptr<WakeupFd>> (*)() {
#ifndef GRPC_POSIX_NO_SPECIAL_WAKEUP_FD
  if (EventFdWakeupFd::IsSupported()) {
    return &EventFdWakeupFd::CreateEventFdWakeupFd;
  }
#endif  // GRPC_POSIX_NO_SPECIAL_WAKEUP_FD
  if (PipeWakeupFd::IsSupported()) {
    return &PipeWakeupFd::CreatePipeWakeupFd;
  }
  return NotSupported;
}();
}  // namespace

bool SupportsWakeupFd() { return g_wakeup_fd_fn != NotSupported; }

absl::StatusOr<std::unique_ptr<WakeupFd>> CreateWakeupFd() {
  return g_wakeup_fd_fn();
}

#else  // GRPC_POSIX_WAKEUP_FD

bool SupportsWakeupFd() { return false; }

absl::StatusOr<std::unique_ptr<WakeupFd>> CreateWakeupFd() {
  return absl::NotFoundError("Wakeup-fd is not supported on this system");
}

#endif  // GRPC_POSIX_WAKEUP_FD

}  // namespace grpc_event_engine::experimental
