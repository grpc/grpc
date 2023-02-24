//
//
// Copyright 2016 gRPC authors.
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
//
//
#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/port.h"
#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)

#include <string.h>
#include <sys/ioctl.h>

#include <ares.h>

#include "absl/strings/str_cat.h"

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/iomgr/ev_posix.h"

namespace grpc_core {

class GrpcPolledFdPosix : public GrpcPolledFd {
 public:
  GrpcPolledFdPosix(ares_socket_t as, grpc_pollset_set* driver_pollset_set)
      : name_(absl::StrCat("c-ares fd: ", static_cast<int>(as))), as_(as) {
    fd_ = grpc_fd_create(static_cast<int>(as), name_.c_str(), false);
    driver_pollset_set_ = driver_pollset_set;
    grpc_pollset_set_add_fd(driver_pollset_set_, fd_);
  }

  ~GrpcPolledFdPosix() override {
    grpc_pollset_set_del_fd(driver_pollset_set_, fd_);
    // c-ares library will close the fd inside grpc_fd. This fd may be picked up
    // immediately by another thread, and should not be closed by the following
    // grpc_fd_orphan.
    int phony_release_fd;
    grpc_fd_orphan(fd_, nullptr, &phony_release_fd, "c-ares query finished");
  }

  void RegisterForOnReadableLocked(grpc_closure* read_closure)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) override {
    grpc_fd_notify_on_read(fd_, read_closure);
  }

  void RegisterForOnWriteableLocked(grpc_closure* write_closure)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) override {
    grpc_fd_notify_on_write(fd_, write_closure);
  }

  bool IsFdStillReadableLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) override {
    size_t bytes_available = 0;
    return ioctl(grpc_fd_wrapped_fd(fd_), FIONREAD, &bytes_available) == 0 &&
           bytes_available > 0;
  }

  void ShutdownLocked(grpc_error_handle error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) override {
    grpc_fd_shutdown(fd_, error);
  }

  ares_socket_t GetWrappedAresSocketLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&grpc_ares_request::mu) override {
    return as_;
  }

  const char* GetName() const override { return name_.c_str(); }

 private:
  const std::string name_;
  const ares_socket_t as_;
  grpc_fd* fd_ ABSL_GUARDED_BY(&grpc_ares_request::mu);
  grpc_pollset_set* driver_pollset_set_ ABSL_GUARDED_BY(&grpc_ares_request::mu);
};

class GrpcPolledFdFactoryPosix : public GrpcPolledFdFactory {
 public:
  GrpcPolledFd* NewGrpcPolledFdLocked(
      ares_socket_t as, grpc_pollset_set* driver_pollset_set) override {
    return new GrpcPolledFdPosix(as, driver_pollset_set);
  }

  void ConfigureAresChannelLocked(ares_channel /*channel*/) override {}
};

std::unique_ptr<GrpcPolledFdFactory> NewGrpcPolledFdFactory(Mutex* /* mu */) {
  return std::make_unique<GrpcPolledFdFactoryPosix>();
}

}  // namespace grpc_core

#endif  // GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
