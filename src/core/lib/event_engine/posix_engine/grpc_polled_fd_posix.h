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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_GRPC_POLLED_FD_POSIX_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_GRPC_POLLED_FD_POSIX_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/lib/event_engine/posix_engine/posix_interface.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/sync.h"

#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)

// IWYU pragma: no_include <ares_build.h>

#include <ares.h>
#include <sys/ioctl.h>

#include <string>
#include <unordered_set>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_closure.h"

namespace grpc_event_engine::experimental {

class GrpcPolledFdPosix : public GrpcPolledFd {
 public:
  GrpcPolledFdPosix(ares_socket_t as, EventHandle* handle)
      : name_(absl::StrCat("c-ares fd: ", static_cast<int>(as))),
        as_(as),
        handle_(handle) {}

  ~GrpcPolledFdPosix() override {
    // c-ares library will close the fd. This fd may be picked up immediately by
    // another thread and should not be closed by the following OrphanHandle.
    FileDescriptor phony_release_fd;
    handle_->OrphanHandle(/*on_done=*/nullptr, &phony_release_fd,
                          "c-ares query finished");
  }

  void RegisterForOnReadableLocked(
      absl::AnyInvocable<void(absl::Status)> read_closure) override {
    handle_->NotifyOnRead(new PosixEngineClosure(std::move(read_closure),
                                                 /*is_permanent=*/false));
  }

  void RegisterForOnWriteableLocked(
      absl::AnyInvocable<void(absl::Status)> write_closure) override {
    handle_->NotifyOnWrite(new PosixEngineClosure(std::move(write_closure),
                                                  /*is_permanent=*/false));
  }

  bool IsFdStillReadableLocked() override {
    size_t bytes_available = 0;
    return handle_->Poller()
               ->posix_interface()
               .Ioctl(handle_->WrappedFd(), FIONREAD, &bytes_available)
               .ok() &&
           bytes_available > 0;
  }

  bool ShutdownLocked(absl::Status error) override {
    handle_->ShutdownHandle(error);
    return true;
  }

  ares_socket_t GetWrappedAresSocketLocked() override { return as_; }

  const char* GetName() const override { return name_.c_str(); }

 private:
  const std::string name_;
  const ares_socket_t as_;
  EventHandle* handle_;
};

class GrpcPolledFdFactoryPosix : public GrpcPolledFdFactory {
 public:
  explicit GrpcPolledFdFactoryPosix(PosixEventPoller* poller)
      : poller_(poller) {}

  ~GrpcPolledFdFactoryPosix() override {
    for (auto& fd : owned_fds_) {
      close(fd);
    }
  }

  void Initialize(grpc_core::Mutex*, EventEngine*) override {}

  std::unique_ptr<GrpcPolledFd> NewGrpcPolledFdLocked(
      ares_socket_t as) override {
    auto fd = poller_->posix_interface().FromInteger(as);
    if (!fd.ok()) {
      return nullptr;
    }
    owned_fds_.insert(as);
    return std::make_unique<GrpcPolledFdPosix>(
        as, poller_->CreateHandle(fd.fd(), "c-ares socket",
                                  poller_->CanTrackErrors()));
  }

  void ConfigureAresChannelLocked(ares_channel channel) override {
    ares_set_socket_functions(channel, &kSockFuncs, this);
    ares_set_socket_configure_callback(
        channel, &GrpcPolledFdFactoryPosix::ConfigureSocket, this);
  }

 private:
  /// Overridden socket API for c-ares
  static ares_socket_t Socket(int af, int type, int protocol,
                              void* polled_fd_factory) {
    auto& posix_interface =
        static_cast<GrpcPolledFdFactoryPosix*>(polled_fd_factory)
            ->poller_->posix_interface();
    return posix_interface.Socket(af, type, protocol)
        .if_ok(-1, [&](const FileDescriptor& fd) {
          return posix_interface.ToInteger(fd);
        });
  }

  /// Overridden connect API for c-ares
  static int Connect(ares_socket_t as, const struct sockaddr* target,
                     ares_socklen_t target_len, void* polled_fd_factory) {
    auto& posix_interface =
        static_cast<GrpcPolledFdFactoryPosix*>(polled_fd_factory)
            ->poller_->posix_interface();
    return posix_interface.FromInteger(as).if_ok(
        -1, [&](const FileDescriptor& fd) {
          return posix_interface.Connect(fd, target, target_len).ok() ? 0 : -1;
        });
  }

  /// Overridden writev API for c-ares
  static ares_ssize_t WriteV(ares_socket_t as, const struct iovec* iov,
                             int iovec_count, void* polled_fd_factory) {
    auto& posix_interface =
        static_cast<GrpcPolledFdFactoryPosix*>(polled_fd_factory)
            ->poller_->posix_interface();
    return posix_interface.FromInteger(as).if_ok(
        -1, [&](const FileDescriptor& fd) {
          auto result = posix_interface.WriteV(fd, iov, iovec_count);
          return result.ok() ? *result : -1;
        });
  }

  /// Overridden recvfrom API for c-ares
  static ares_ssize_t RecvFrom(ares_socket_t as, void* data, size_t data_len,
                               int flags, struct sockaddr* from,
                               ares_socklen_t* from_len,
                               void* polled_fd_factory) {
    auto& posix_interface =
        static_cast<GrpcPolledFdFactoryPosix*>(polled_fd_factory)
            ->poller_->posix_interface();
    return posix_interface.FromInteger(as).if_ok(
        -1, [&](const FileDescriptor& fd) {
          auto result = posix_interface.RecvFrom(fd, data, data_len, flags,
                                                 from, from_len);
          return result.ok() ? *result : -1;
        });
  }

  /// Overridden close API for c-ares
  static int Close(ares_socket_t as, void* polled_fd_factory) {
    GrpcPolledFdFactoryPosix* self =
        static_cast<GrpcPolledFdFactoryPosix*>(polled_fd_factory);
    if (self->owned_fds_.find(as) == self->owned_fds_.end()) {
      // c-ares owns this fd, grpc has never seen it
      auto& posix_interface = self->poller_->posix_interface();
      return posix_interface.FromInteger(as).if_ok(0, [&](const auto& fd) {
        posix_interface.Close(fd);
        return 0;
      });
    }
    return 0;
  }

  /// Because we're using socket API overrides, c-ares won't
  /// perform its typical configuration on the socket. See
  /// https://github.com/c-ares/c-ares/blob/bad62225b7f6b278b92e8e85a255600b629ef517/src/lib/ares_process.c#L1018.
  /// So we use the configure socket callback override and copy default
  /// settings that c-ares would normally apply on posix platforms:
  ///   - non-blocking
  ///   - cloexec flag
  ///   - disable nagle
  static int ConfigureSocket(ares_socket_t fd, int type,
                             void* polled_fd_factory) {
    auto& posix_interface =
        static_cast<GrpcPolledFdFactoryPosix*>(polled_fd_factory)
            ->poller_->posix_interface();
    return posix_interface.FromInteger(fd).if_ok(
        -1, [&](const FileDescriptor& fd) {
          return posix_interface.ConfigureSocket(fd, type);
        });
  }

  const struct ares_socket_functions kSockFuncs = {
      &GrpcPolledFdFactoryPosix::Socket /* socket */,
      &GrpcPolledFdFactoryPosix::Close /* close */,
      &GrpcPolledFdFactoryPosix::Connect /* connect */,
      &GrpcPolledFdFactoryPosix::RecvFrom /* recvfrom */,
      &GrpcPolledFdFactoryPosix::WriteV /* writev */,
  };

  PosixEventPoller* poller_;
  // posix_interface that are used/owned by grpc - we (grpc) will close them
  // rather than c-ares
  std::unordered_set<ares_socket_t> owned_fds_;
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_GRPC_POLLED_FD_POSIX_H
