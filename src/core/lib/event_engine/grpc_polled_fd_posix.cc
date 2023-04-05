// Copyright 2023 The gRPC Authors
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
#include <string>
#include <utility>

#include <ares.h>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "src/core/lib/event_engine/ares_driver.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/port.h"

#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)

#include <string.h>
#include <sys/ioctl.h>

#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"

namespace grpc_event_engine {
namespace experimental {

class GrpcPolledFdPosix : public GrpcPolledFd {
 public:
  GrpcPolledFdPosix(ares_socket_t as, PollerHandle poller_handle)
      : name_(absl::StrCat("c-ares fd: ", static_cast<int>(as))),
        as_(as),
        poller_handle_(poller_handle) {}

  ~GrpcPolledFdPosix() override {
    // c-ares library will close the fd inside grpc_fd. This fd may be picked up
    // immediately by another thread, and should not be closed by the following
    // grpc_fd_orphan.
    int phony_release_fd;
    poller_handle_->OrphanHandle(/*on_done*/ nullptr, &phony_release_fd,
                                 "c-ares query finished");
  }

  void RegisterForOnReadableLocked(
      absl::AnyInvocable<void(absl::Status)> read_closure) override {
    poller_handle_->NotifyOnRead(new PosixEngineClosure(
        std::move(read_closure), /*is_permanent=*/false));
  }

  void RegisterForOnWriteableLocked(
      absl::AnyInvocable<void(absl::Status)> write_closure) override {
    poller_handle_->NotifyOnWrite(new PosixEngineClosure(
        std::move(write_closure), /*is_permanent=*/false));
  }

  bool IsFdStillReadableLocked() override {
    size_t bytes_available = 0;
    return ioctl(poller_handle_->WrappedFd(), FIONREAD, &bytes_available) ==
               0 &&
           bytes_available > 0;
  }

  void ShutdownLocked(grpc_error_handle error) override {
    poller_handle_->ShutdownHandle(error);
  }

  ares_socket_t GetWrappedAresSocketLocked() override { return as_; }

  const char* GetName() const override { return name_.c_str(); }

 private:
  const std::string name_;
  const ares_socket_t as_;
  const PollerHandle poller_handle_;
};

class GrpcPolledFdFactoryPosix : public GrpcPolledFdFactory {
 public:
  explicit GrpcPolledFdFactoryPosix(
      RegisterAresSocketWithPollerCallback register_cb)
      : register_cb_(std::move(register_cb)) {}

  GrpcPolledFd* NewGrpcPolledFdLocked(ares_socket_t as) override {
    return new GrpcPolledFdPosix(as, register_cb_(as));
  }

  void ConfigureAresChannelLocked(ares_channel /*channel*/) override {}

 private:
  RegisterAresSocketWithPollerCallback register_cb_;
};

std::unique_ptr<GrpcPolledFdFactory> NewGrpcPolledFdFactory(
    RegisterAresSocketWithPollerCallback register_cb,
    grpc_core::Mutex* /* mu */) {
  return std::make_unique<GrpcPolledFdFactoryPosix>(std::move(register_cb));
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
