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
#include <utility>

#include "absl/strings/str_cat.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/crash.h"  // IWYU pragma: keep

#ifdef GRPC_POSIX_WAKEUP_FD
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "src/core/lib/event_engine/posix_engine/wakeup_fd_posix.h"
#endif

#include "src/core/lib/event_engine/posix_engine/wakeup_fd_pipe.h"
#include "src/core/util/strerror.h"

namespace grpc_event_engine::experimental {

#ifdef GRPC_POSIX_WAKEUP_FD

namespace {

absl::Status SetSocketNonBlocking(int fd) {
  int oldflags = fcntl(fd, F_GETFL, 0);
  if (oldflags < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  oldflags |= O_NONBLOCK;

  if (fcntl(fd, F_SETFL, oldflags) != 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  return absl::OkStatus();
}
}  // namespace

absl::Status PipeWakeupFd::Init() {
  int pipefd[2];
  int r = pipe(pipefd);
  if (0 != r) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("pipe: ", grpc_core::StrError(errno)));
  }
  auto status = SetSocketNonBlocking(pipefd[0]);
  if (!status.ok()) {
    close(pipefd[0]);
    close(pipefd[1]);
    return status;
  }
  status = SetSocketNonBlocking(pipefd[1]);
  if (!status.ok()) {
    close(pipefd[0]);
    close(pipefd[1]);
    return status;
  }
  SetWakeupFds(pipefd[0], pipefd[1]);
  return absl::OkStatus();
}

absl::Status PipeWakeupFd::ConsumeWakeup() {
  char buf[128];
  ssize_t r;

  for (;;) {
    r = read(ReadFd(), buf, sizeof(buf));
    if (r > 0) continue;
    if (r == 0) return absl::OkStatus();
    switch (errno) {
      case EAGAIN:
        return absl::OkStatus();
      case EINTR:
        continue;
      default:
        return absl::Status(absl::StatusCode::kInternal,
                            absl::StrCat("read: ", grpc_core::StrError(errno)));
    }
  }
}

absl::Status PipeWakeupFd::Wakeup() {
  char c = 0;
  while (write(WriteFd(), &c, 1) != 1 && errno == EINTR) {
  }
  return absl::OkStatus();
}

PipeWakeupFd::~PipeWakeupFd() {
  if (ReadFd() != 0) {
    close(ReadFd());
  }
  if (WriteFd() != 0) {
    close(WriteFd());
  }
}

bool PipeWakeupFd::IsSupported() {
  PipeWakeupFd pipe_wakeup_fd;
  return pipe_wakeup_fd.Init().ok();
}

absl::StatusOr<std::unique_ptr<WakeupFd>> PipeWakeupFd::CreatePipeWakeupFd() {
  static bool kIsPipeWakeupFdSupported = PipeWakeupFd::IsSupported();
  if (kIsPipeWakeupFdSupported) {
    auto pipe_wakeup_fd = std::make_unique<PipeWakeupFd>();
    auto status = pipe_wakeup_fd->Init();
    if (status.ok()) {
      return std::unique_ptr<WakeupFd>(std::move(pipe_wakeup_fd));
    }
    return status;
  }
  return absl::NotFoundError("Pipe wakeup fd is not supported");
}

#else  //  GRPC_POSIX_WAKEUP_FD

absl::Status PipeWakeupFd::Init() { grpc_core::Crash("unimplemented"); }

absl::Status PipeWakeupFd::ConsumeWakeup() {
  grpc_core::Crash("unimplemented");
}

absl::Status PipeWakeupFd::Wakeup() { grpc_core::Crash("unimplemented"); }

bool PipeWakeupFd::IsSupported() { return false; }

absl::StatusOr<std::unique_ptr<WakeupFd>> PipeWakeupFd::CreatePipeWakeupFd() {
  return absl::NotFoundError("Pipe wakeup fd is not supported");
}

#endif  //  GRPC_POSIX_WAKEUP_FD

}  // namespace grpc_event_engine::experimental
