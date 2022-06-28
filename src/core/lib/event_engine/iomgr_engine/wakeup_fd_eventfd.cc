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

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_LINUX_EVENTFD

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "src/core/lib/event_engine/iomgr_engine/wakeup_fd_posix.h"
#endif

#include "src/core/lib/event_engine/iomgr_engine/wakeup_fd_eventfd.h"

namespace grpc_event_engine {
namespace iomgr_engine {

#ifdef GRPC_LINUX_EVENTFD

absl::Status EventFdWakeupFd::Init() {
  this->read_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  this->write_fd_ = -1;
  if (this->read_fd_ < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("eventfd: ", strerror(errno)));
  }
  return absl::OkStatus();
}

absl::Status EventFdWakeupFd::ConsumeWakeup() {
  eventfd_t value;
  int err;
  do {
    err = eventfd_read(this->read_fd_, &value);
  } while (err < 0 && errno == EINTR);
  if (err < 0 && errno != EAGAIN) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("eventfd_read: ", strerror(errno)));
  }
  return absl::OkStatus();
}

absl::Status EventFdWakeupFd::Wakeup() {
  int err;
  do {
    err = eventfd_write(this->read_fd_, 1);
  } while (err < 0 && errno == EINTR);
  if (err < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("eventfd_write: ", strerror(errno)));
  }
  return absl::OkStatus();
}

void EventFdWakeupFd::Destroy() {
  if (this->read_fd_ != 0) {
    close(this->read_fd_);
    this->read_fd_ = 0;
  }
}

bool EventFdWakeupFd::IsSupported() {
  EventFdWakeupFd event_fd_wakeup_fd;
  if (event_fd_wakeup_fd.Init().ok()) {
    event_fd_wakeup_fd.Destroy();
    return true;
  } else {
    return false;
  }
}

absl::StatusOr<std::unique_ptr<WakeupFd>>
EventFdWakeupFd::CreateEventFdWakeupFd() {
  static bool kIsEventFdWakeupFdSupported = EventFdWakeupFd::IsSupported();
  if (kIsEventFdWakeupFdSupported) {
    auto event_fd_wakeup_fd = absl::make_unique<EventFdWakeupFd>();
    auto status = event_fd_wakeup_fd->Init();
    if (status.ok()) {
      return event_fd_wakeup_fd;
    }
    return status;
  }
  return absl::NotFoundError("Eventfd wakeup fd is not supported");
}

#else  //  GRPC_LINUX_EVENTFD

absl::Status EventFdWakeupFd::Init() { GPR_ASSERT(false && "unimplemented"); }

absl::Status EventFdWakeupFd::ConsumeWakeup() {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status EventFdWakeupFd::Wakeup() { GPR_ASSERT(false && "unimplemented"); }

void EventFdWakeupFd::Destroy() { GPR_ASSERT(false && "unimplemented"); }

bool EventFdWakeupFd::IsSupported() { return false; }

absl::StatusOr<std::unique_ptr<WakeupFd>>
EventFdWakeupFd::CreateEventFdWakeupFd() {
  return absl::NotFoundError("Eventfd wakeup fd is not supported");
}

#endif  // GRPC_LINUX_EVENTFD

}  // namespace iomgr_engine
}  // namespace grpc_event_engine
