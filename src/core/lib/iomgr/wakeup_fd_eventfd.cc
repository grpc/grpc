//
//
// Copyright 2015 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_LINUX_EVENTFD

#include <errno.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "src/core/util/crash.h"
#include "src/core/util/strerror.h"

static grpc_error_handle eventfd_create(grpc_wakeup_fd* fd_info) {
  fd_info->read_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  fd_info->write_fd = -1;
  if (fd_info->read_fd < 0) {
    return GRPC_OS_ERROR(errno, "eventfd");
  }
  return absl::OkStatus();
}

static grpc_error_handle eventfd_consume(grpc_wakeup_fd* fd_info) {
  eventfd_t value;
  int err;
  do {
    err = eventfd_read(fd_info->read_fd, &value);
  } while (err < 0 && errno == EINTR);
  if (err < 0 && errno != EAGAIN) {
    return GRPC_OS_ERROR(errno, "eventfd_read");
  }
  return absl::OkStatus();
}

static grpc_error_handle eventfd_wakeup(grpc_wakeup_fd* fd_info) {
  int err;
  do {
    err = eventfd_write(fd_info->read_fd, 1);
  } while (err < 0 && errno == EINTR);
  if (err < 0) {
    return GRPC_OS_ERROR(errno, "eventfd_write");
  }
  return absl::OkStatus();
}

static void eventfd_destroy(grpc_wakeup_fd* fd_info) {
  if (fd_info->read_fd != 0) close(fd_info->read_fd);
}

static int eventfd_check_availability(void) {
  const int efd = eventfd(0, 0);
  const int is_available = efd >= 0;
  if (is_available) close(efd);
  return is_available;
}

const grpc_wakeup_fd_vtable grpc_specialized_wakeup_fd_vtable = {
    eventfd_create, eventfd_consume, eventfd_wakeup, eventfd_destroy,
    eventfd_check_availability};

#endif  // GRPC_LINUX_EVENTFD
