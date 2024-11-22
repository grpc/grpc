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

#include "src/core/lib/event_engine/posix_engine/posix_system_api.h"
#include "src/core/lib/event_engine/posix_engine/wakeup_fd_pipe.h"
#include "src/core/util/strerror.h"

namespace grpc_event_engine {
namespace experimental {

#ifdef GRPC_POSIX_WAKEUP_FD

absl::Status PipeWakeupFd::Init(const SystemApi& system_api) {
  auto r = system_api.Pipe();
  if (0 != r.first) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("pipe: ", grpc_core::StrError(errno)));
  }
  auto pipefd = r.second;
  auto status = system_api.SetSocketNonBlocking(pipefd[0]);
  if (!status.ok()) return status;
  status = system_api.SetSocketNonBlocking(pipefd[1]);
  if (!status.ok()) return status;
  SetWakeupFds(pipefd[0], pipefd[1]);
  return absl::OkStatus();
}

absl::Status PipeWakeupFd::ConsumeWakeup() {
  char buf[128];
  ssize_t r;

  for (;;) {
    r = system_api_->Read(ReadFd(), buf, sizeof(buf));
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
  while (system_api_->Write(WriteFd(), &c, 1) != 1 && errno == EINTR) {
  }
  return absl::OkStatus();
}

PipeWakeupFd::~PipeWakeupFd() {
  if (ReadFd().ready()) {
    system_api_->Close(ReadFd());
  }
  if (WriteFd().ready()) {
    system_api_->Close(WriteFd());
  }
}

bool PipeWakeupFd::IsSupported(const SystemApi& system_api) {
  PipeWakeupFd pipe_wakeup_fd(&system_api);
  return pipe_wakeup_fd.Init(system_api).ok();
}

absl::StatusOr<std::unique_ptr<WakeupFd>> PipeWakeupFd::CreatePipeWakeupFd(
    const SystemApi& system_api) {
  static bool kIsPipeWakeupFdSupported = PipeWakeupFd::IsSupported(system_api);
  if (kIsPipeWakeupFdSupported) {
    auto pipe_wakeup_fd = std::make_unique<PipeWakeupFd>(&system_api);
    auto status = pipe_wakeup_fd->Init(system_api);
    if (status.ok()) {
      return std::unique_ptr<WakeupFd>(std::move(pipe_wakeup_fd));
    }
    return status;
  }
  return absl::NotFoundError("Pipe wakeup fd is not supported");
}

#else  //  GRPC_POSIX_WAKEUP_FD

absl::Status PipeWakeupFd::Init(const SystemApi& system_api) {
  grpc_core::Crash("unimplemented");
}

absl::Status PipeWakeupFd::ConsumeWakeup() {
  grpc_core::Crash("unimplemented");
}

absl::Status PipeWakeupFd::Wakeup() { grpc_core::Crash("unimplemented"); }

bool PipeWakeupFd::IsSupported(const SystemApi& system_api) { return false; }

absl::StatusOr<std::unique_ptr<WakeupFd>> PipeWakeupFd::CreatePipeWakeupFd(
    const SystemApi& system_api) {
  return absl::NotFoundError("Pipe wakeup fd is not supported");
}

#endif  //  GRPC_POSIX_WAKEUP_FD

}  // namespace experimental
}  // namespace grpc_event_engine
