/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_WAKEUP_FD

#include "src/core/lib/iomgr/wakeup_fd_posix.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/socket_utils_posix.h"

static void pipe_init(grpc_wakeup_fd* fd_info) {
  int pipefd[2];
  /* TODO(klempner): Make this nonfatal */
  int r = pipe(pipefd);
  if (0 != r) {
    gpr_log(GPR_ERROR, "pipe creation failed (%d): %s", errno, strerror(errno));
    abort();
  }
  GPR_ASSERT(grpc_set_socket_nonblocking(pipefd[0], 1));
  GPR_ASSERT(grpc_set_socket_nonblocking(pipefd[1], 1));
  fd_info->read_fd = pipefd[0];
  fd_info->write_fd = pipefd[1];
}

static void pipe_consume(grpc_wakeup_fd* fd_info) {
  char buf[128];
  ssize_t r;

  for (;;) {
    r = read(fd_info->read_fd, buf, sizeof(buf));
    if (r > 0) continue;
    if (r == 0) return;
    switch (errno) {
      case EAGAIN:
        return;
      case EINTR:
        continue;
      default:
        gpr_log(GPR_ERROR, "error reading pipe: %s", strerror(errno));
        return;
    }
  }
}

static void pipe_wakeup(grpc_wakeup_fd* fd_info) {
  char c = 0;
  while (write(fd_info->write_fd, &c, 1) != 1 && errno == EINTR)
    ;
}

static void pipe_destroy(grpc_wakeup_fd* fd_info) {
  if (fd_info->read_fd != 0) close(fd_info->read_fd);
  if (fd_info->write_fd != 0) close(fd_info->write_fd);
}

static int pipe_check_availability(void) {
  /* Assume that pipes are always available. */
  return 1;
}

const grpc_wakeup_fd_vtable grpc_pipe_wakeup_fd_vtable = {
    pipe_init, pipe_consume, pipe_wakeup, pipe_destroy,
    pipe_check_availability};

#endif /* GPR_POSIX_WAKUP_FD */
