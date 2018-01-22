/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

/* This is only relevant on linux kernels supporting epoll() */
#ifdef GRPC_LINUX_EPOLL

#include <fcntl.h>
#include <sys/epoll.h>

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/ev_epoll_linux.h"

int epoll_create_and_cloexec() {
#ifdef GRPC_LINUX_EPOLL_CREATE1
  int fd = epoll_create1(EPOLL_CLOEXEC);
#else  /* !defined(GRPC_LINUX_EPOLL_CREATE1) */
  int fd = epoll_create(100);
  if (fd >= 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
    gpr_log(GPR_ERROR, "failed to set FD_CLOEXEC flag after epoll_create");
    return -1;
  }
#endif /* defined(GRPC_LINUX_EPOLL_CREATE1) */
  return fd;
}

#endif /* GRPC_LINUX_EPOLL */
