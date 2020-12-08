/*
 *
 * Copyright 2015 gRPC authors.
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

#ifdef GRPC_POSIX_WAKEUP_FD

#include "src/core/lib/iomgr/wakeup_fd_pipe.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/socket_utils_posix.h"

static grpc_error* pipe_init(grpc_wakeup_fd* fd_info) {
  int pipefd[2];
  int r = pipe(pipefd);
  if (0 != r) {
    gpr_log(GPR_ERROR, "pipe creation failed (%d): %s", errno, strerror(errno));
    return GRPC_OS_ERROR(errno, "pipe");
  }
  grpc_error* err;
  err = grpc_set_socket_nonblocking(pipefd[0], 1);
  if (err != GRPC_ERROR_NONE) return err;
  err = grpc_set_socket_nonblocking(pipefd[1], 1);
  if (err != GRPC_ERROR_NONE) return err;
  fd_info->read_fd = pipefd[0];
  fd_info->write_fd = pipefd[1];
  return GRPC_ERROR_NONE;
}

static grpc_error* pipe_consume(grpc_wakeup_fd* fd_info) {
  char buf[128];
  ssize_t r;

  for (;;) {
    r = read(fd_info->read_fd, buf, sizeof(buf));
    if (r > 0) continue;
    if (r == 0) return GRPC_ERROR_NONE;
    switch (errno) {
      case EAGAIN:
        return GRPC_ERROR_NONE;
      case EINTR:
        continue;
      default:
        return GRPC_OS_ERROR(errno, "read");
    }
  }
}

static grpc_error* pipe_wakeup(grpc_wakeup_fd* fd_info) {
  char c = 0;
  while (write(fd_info->write_fd, &c, 1) != 1 && errno == EINTR) {
  }
  return GRPC_ERROR_NONE;
}

static void pipe_destroy(grpc_wakeup_fd* fd_info) {
  if (fd_info->read_fd != 0) close(fd_info->read_fd);
  if (fd_info->write_fd != 0) close(fd_info->write_fd);
}

static int pipe_check_availability(void) {
  grpc_wakeup_fd fd;
  fd.read_fd = fd.write_fd = -1;

  if (pipe_init(&fd) == GRPC_ERROR_NONE) {
    pipe_destroy(&fd);
    return 1;
  } else {
    return 0;
  }
}

const grpc_wakeup_fd_vtable grpc_pipe_wakeup_fd_vtable = {
    pipe_init, pipe_consume, pipe_wakeup, pipe_destroy,
    pipe_check_availability};

#endif /* GPR_POSIX_WAKUP_FD */
