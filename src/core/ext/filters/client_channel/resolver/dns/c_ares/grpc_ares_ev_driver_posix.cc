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
#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET)

#include <ares.h>
#include <string.h>
#include <sys/ioctl.h>

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

class AresEvDriverPosix;

class FdNodePosix final : public FdNode {
 public:
  FdNodePosix(grpc_fd* fd) : FdNode() { fd_ = fd; }
  ~FdNodePosix() {
    gpr_log(GPR_DEBUG, "delete fd: %" PRIdPTR, (uintptr_t)GetInnerEndpoint());
    /* c-ares library has closed the fd inside grpc_fd. This fd may be picked up
       immediately by another thread, and should not be closed by the following
       grpc_fd_orphan. */
    grpc_fd_orphan(fd_, nullptr, nullptr, true /* already_closed */,
                   "c-ares query finished");
  }

 private:
  void ShutdownInnerEndpoint() override {
    grpc_fd_shutdown(
        fd_, GRPC_ERROR_CREATE_FROM_STATIC_STRING("c-ares fd shutdown"));
  }

  ares_socket_t GetInnerEndpoint() override { return grpc_fd_wrapped_fd(fd_); }

  bool ShouldRepeatReadForAresProcessFd() override {
    const int fd = grpc_fd_wrapped_fd(fd_);
    size_t bytes_available = 0;
    return ioctl(fd, FIONREAD, &bytes_available) == 0 && bytes_available > 0;
  }

  void RegisterForOnReadable() override {
    gpr_log(GPR_DEBUG, "notify read on: %d", grpc_fd_wrapped_fd(fd_));
    grpc_fd_notify_on_read(fd_, &read_closure_);
  }
  void RegisterForOnWriteable() override {
    gpr_log(GPR_DEBUG, "notify read on: %d", grpc_fd_wrapped_fd(fd_));
    grpc_fd_notify_on_write(fd_, &write_closure_);
  }

 private:
  grpc_fd* fd_;
};

class AresEvDriverPosix final : public AresEvDriver {
 public:
  AresEvDriverPosix(grpc_pollset_set* pollset_set) : AresEvDriver() {
    pollset_set_ = pollset_set;
  }

 private:
  FdNode* CreateFdNode(ares_socket_t as, const char* name) override {
    grpc_fd* fd = grpc_fd_create(as, name);
    grpc_pollset_set_add_fd(pollset_set_, fd);
    return grpc_core::New<FdNodePosix>(fd);
  }
  grpc_pollset_set* pollset_set_;
};

AresEvDriver* AresEvDriver::Create(grpc_pollset_set* pollset_set) {
  return grpc_core::New<AresEvDriverPosix>(pollset_set);
}

}  // namespace grpc_core

#endif /* GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET) */
