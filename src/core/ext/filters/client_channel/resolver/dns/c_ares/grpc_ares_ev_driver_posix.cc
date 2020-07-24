/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"
#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)

#include <ares.h>
#include <string.h>
#include <sys/ioctl.h>

#include "absl/strings/str_cat.h"

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

namespace grpc_core {

class GrpcPolledFdPosix : public GrpcPolledFd {
 public:
  GrpcPolledFdPosix(ares_socket_t as, grpc_pollset_set* driver_pollset_set)
      : name_(absl::StrCat("c-ares fd: ", (int)as)), as_(as) {
    fd_ = grpc_fd_create((int)as, name_.c_str(), false);
    driver_pollset_set_ = driver_pollset_set;
    grpc_pollset_set_add_fd(driver_pollset_set_, fd_);
  }

  ~GrpcPolledFdPosix() {
    grpc_pollset_set_del_fd(driver_pollset_set_, fd_);
    /* c-ares library will close the fd inside grpc_fd. This fd may be picked up
       immediately by another thread, and should not be closed by the following
       grpc_fd_orphan. */
    int dummy_release_fd;
    grpc_fd_orphan(fd_, nullptr, &dummy_release_fd, "c-ares query finished");
  }

  void RegisterForOnReadableLocked(grpc_closure* read_closure) override {
    grpc_fd_notify_on_read(fd_, read_closure);
  }

  void RegisterForOnWriteableLocked(grpc_closure* write_closure) override {
    grpc_fd_notify_on_write(fd_, write_closure);
  }

  bool IsFdStillReadableLocked() override {
    size_t bytes_available = 0;
    return ioctl(grpc_fd_wrapped_fd(fd_), FIONREAD, &bytes_available) == 0 &&
           bytes_available > 0;
  }

  void ShutdownLocked(grpc_error* error) override {
    grpc_fd_shutdown(fd_, error);
  }

  ares_socket_t GetWrappedAresSocketLocked() override { return as_; }

  const char* GetName() override { return name_.c_str(); }

 private:
  std::string name_;
  ares_socket_t as_;
  grpc_fd* fd_;
  grpc_pollset_set* driver_pollset_set_;
};

class GrpcPolledFdFactoryPosix : public GrpcPolledFdFactory {
 public:
  GrpcPolledFd* NewGrpcPolledFdLocked(
      ares_socket_t as, grpc_pollset_set* driver_pollset_set,
      std::shared_ptr<WorkSerializer> /*work_serializer*/) override {
    return new GrpcPolledFdPosix(as, driver_pollset_set);
  }

  void ConfigureAresChannelLocked(ares_channel /*channel*/) override {}
};

std::unique_ptr<GrpcPolledFdFactory> NewGrpcPolledFdFactory(
    std::shared_ptr<WorkSerializer> work_serializer) {
  (void)work_serializer;
  return absl::make_unique<GrpcPolledFdFactoryPosix>();
}

}  // namespace grpc_core

#endif /* GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER) */
