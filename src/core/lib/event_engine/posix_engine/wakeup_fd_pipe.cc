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
#include "src/core/lib/event_engine/posix_engine/posix_interface.h"
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

absl::Status PipeWakeupFd::Init() {
  auto pipe_ends = posix_interface_->Pipe();
  if (!pipe_ends.ok()) return std::move(pipe_ends).status();
  SetWakeupFds(pipe_ends->first, pipe_ends->second);
  return absl::OkStatus();
}

absl::Status PipeWakeupFd::ConsumeWakeup() {
  std::array<char, 128> buf;
  for (;;) {
    auto r = posix_interface_->Read(ReadFd(), absl::Span<char>(buf));
    if (r.ok()) {
      if (*r > 0) continue;
      if (*r == 0) return absl::OkStatus();
    } else if (r.IsWrongGenerationError()) {
      return absl::Status(absl::StatusCode::kInternal,
                          absl::StrCat("read: wrong fd generation"));
    } else {
      switch (*r.errno_value()) {
        case EAGAIN:
          return absl::OkStatus();
        case EINTR:
          continue;
        default:
          return absl::Status(
              absl::StatusCode::kInternal,
              absl::StrCat("read: ", grpc_core::StrError(errno)));
      }
    }
  }
}

absl::Status PipeWakeupFd::Wakeup() {
  char c = 0;
  while (posix_interface_->Write(WriteFd(), absl::Span<char>(&c, 1))
             .IsPosixError(EINTR)) {
  }
  return absl::OkStatus();
}

PipeWakeupFd::~PipeWakeupFd() {
  if (ReadFd().ready()) {
    posix_interface_->Close(ReadFd());
  }
  if (WriteFd().ready()) {
    posix_interface_->Close(WriteFd());
  }
}

bool PipeWakeupFd::IsSupported() {
  EventEnginePosixInterface posix_interface;
  PipeWakeupFd pipe_wakeup_fd(&posix_interface);
  return pipe_wakeup_fd.Init().ok();
}

absl::StatusOr<std::unique_ptr<WakeupFd>> PipeWakeupFd::CreatePipeWakeupFd(
    EventEnginePosixInterface* posix_interface) {
  static bool kIsPipeWakeupFdSupported = PipeWakeupFd::IsSupported();
  if (kIsPipeWakeupFdSupported) {
    auto pipe_wakeup_fd = std::make_unique<PipeWakeupFd>(posix_interface);
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

absl::StatusOr<std::unique_ptr<WakeupFd>> PipeWakeupFd::CreatePipeWakeupFd(
    EventEnginePosixInterface* posix_interface) {
  return absl::NotFoundError("Pipe wakeup fd is not supported");
}

#endif  //  GRPC_POSIX_WAKEUP_FD

}  // namespace grpc_event_engine::experimental
