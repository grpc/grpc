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

#include <utility>

#include "absl/strings/str_cat.h"
#include "src/core/lib/event_engine/posix_engine/posix_interface.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/crash.h"  // IWYU pragma: keep

#ifdef GRPC_LINUX_EVENTFD

#include <errno.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix.h"
#endif

#include "src/core/lib/event_engine/posix_engine/wakeup_fd_eventfd.h"
#include "src/core/util/strerror.h"

namespace grpc_event_engine::experimental {

#ifdef GRPC_LINUX_EVENTFD

absl::Status EventFdWakeupFd::Init() {
  auto read_fd = posix_interface_->EventFd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (!read_fd.ok()) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("eventfd: ", grpc_core::StrError(errno)));
  }
  SetWakeupFds(read_fd.value(), FileDescriptor::Invalid());
  return absl::OkStatus();
}

absl::Status EventFdWakeupFd::ConsumeWakeup() {
  PosixError err;
  do {
    err = posix_interface_->EventFdRead(ReadFd());
  } while (err.IsPosixError(EINTR));
  if (!err.ok() && !err.IsPosixError(EAGAIN)) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("eventfd_read: ", err.StrError()));
  }
  return absl::OkStatus();
}

absl::Status EventFdWakeupFd::Wakeup() {
  PosixError err;
  do {
    err = posix_interface_->EventFdWrite(ReadFd());
  } while (err.IsPosixError(EINTR));
  if (!err.ok()) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("eventfd_write: ", err.StrError()));
  }
  return absl::OkStatus();
}

EventFdWakeupFd::~EventFdWakeupFd() {
  if (ReadFd().ready()) {
    posix_interface_->Close(ReadFd());
  }
}

bool EventFdWakeupFd::IsSupported() {
  EventEnginePosixInterface posix_interface;
  EventFdWakeupFd event_fd_wakeup_fd(&posix_interface);
  return event_fd_wakeup_fd.Init().ok();
}

absl::StatusOr<std::unique_ptr<WakeupFd>>
EventFdWakeupFd::CreateEventFdWakeupFd(
    EventEnginePosixInterface* posix_interface) {
  static bool kIsEventFdWakeupFdSupported = EventFdWakeupFd::IsSupported();
  if (kIsEventFdWakeupFdSupported) {
    auto event_fd_wakeup_fd =
        std::make_unique<EventFdWakeupFd>(posix_interface);
    auto status = event_fd_wakeup_fd->Init();
    if (status.ok()) {
      return std::unique_ptr<WakeupFd>(std::move(event_fd_wakeup_fd));
    }
    return status;
  }
  return absl::NotFoundError("Eventfd wakeup fd is not supported");
}

#else  //  GRPC_LINUX_EVENTFD

#include "src/core/util/crash.h"

absl::Status EventFdWakeupFd::Init() { grpc_core::Crash("unimplemented"); }

absl::Status EventFdWakeupFd::ConsumeWakeup() {
  grpc_core::Crash("unimplemented");
}

absl::Status EventFdWakeupFd::Wakeup() { grpc_core::Crash("unimplemented"); }

bool EventFdWakeupFd::IsSupported() { return false; }

absl::StatusOr<std::unique_ptr<WakeupFd>>
EventFdWakeupFd::CreateEventFdWakeupFd(
    EventEnginePosixInterface* posix_interface) {
  return absl::NotFoundError("Eventfd wakeup fd is not supported");
}

#endif  // GRPC_LINUX_EVENTFD

}  // namespace grpc_event_engine::experimental
