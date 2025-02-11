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

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/event_engine/posix_engine/fork_support.h"
#include "src/core/lib/event_engine/posix_engine/posix_system_api.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/crash.h"  // IWYU pragma: keep

#ifdef GRPC_LINUX_EVENTFD

#include <errno.h>
#include <sys/eventfd.h>
#include <unistd.h>

#endif

#include "src/core/lib/event_engine/posix_engine/wakeup_fd_eventfd.h"
#include "src/core/util/strerror.h"

namespace grpc_event_engine::experimental {

#ifdef GRPC_LINUX_EVENTFD

EventFdWakeupFd::EventFdWakeupFd(SystemApi* system_api)
    : system_api_(system_api),
      fork_subscription_(system_api_->OnFork([this](auto event) {
        if (event == ForkSupport::ForkEvent::kPostFork) {
          CHECK(Init().ok());
        }
      })) {}

absl::Status EventFdWakeupFd::Init() {
  FileDescriptor read_fd = system_api_->EventFd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (!read_fd.ready()) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("eventfd: ", grpc_core::StrError(errno)));
  }
  SetWakeupFds(read_fd, FileDescriptor());
  return absl::OkStatus();
}

absl::Status EventFdWakeupFd::ConsumeWakeup() {
  eventfd_t value;
  absl::StatusOr<int> err;
  do {
    err = system_api_->EventFdRead(ReadFd(), &value);
  } while (err.ok() && *err < 0 && errno == EINTR);
  if (!err.ok()) {
    return std::move(err).status();
  }
  if (*err < 0 && errno != EAGAIN) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("eventfd_read: ", grpc_core::StrError(errno)));
  }
  return absl::OkStatus();
}

absl::Status EventFdWakeupFd::Wakeup() {
  absl::StatusOr<int> err;
  do {
    err = system_api_->EventFdWrite(ReadFd(), 1);
    if (!err.ok()) {
      return std::move(err).status();
    }
  } while (*err < 0 && errno == EINTR);
  if (*err < 0) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("eventfd_write: ", grpc_core::StrError(errno)));
  }
  return absl::OkStatus();
}

EventFdWakeupFd::~EventFdWakeupFd() {
  if (ReadFd().ready()) {
    system_api_->Close(ReadFd());
  }
}

bool EventFdWakeupFd::IsSupported(SystemApi& system_api) {
  EventFdWakeupFd event_fd_wakeup_fd(&system_api);
  return event_fd_wakeup_fd.Init().ok();
}

absl::StatusOr<std::unique_ptr<WakeupFd>>
EventFdWakeupFd::CreateEventFdWakeupFd(SystemApi& system_api) {
  static bool kIsEventFdWakeupFdSupported =
      EventFdWakeupFd::IsSupported(system_api);
  if (kIsEventFdWakeupFdSupported) {
    auto event_fd_wakeup_fd = std::make_unique<EventFdWakeupFd>(&system_api);
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

bool EventFdWakeupFd::IsSupported(SystemApi& /*system_api*/) { return false; }

absl::StatusOr<std::unique_ptr<WakeupFd>>
EventFdWakeupFd::CreateEventFdWakeupFd(SystemApi& /*system_api*/) {
  return absl::NotFoundError("Eventfd wakeup fd is not supported");
}

#endif  // GRPC_LINUX_EVENTFD

}  // namespace grpc_event_engine::experimental
