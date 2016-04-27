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

#ifdef GPR_POSIX_SOCKETUTILS

#include "src/core/lib/iomgr/socket_utils_posix.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/support/log.h>

int grpc_accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen,
                 int nonblock, int cloexec) {
  int fd, flags;

  fd = accept(sockfd, addr, addrlen);
  if (fd >= 0) {
    if (nonblock) {
      flags = fcntl(fd, F_GETFL, 0);
      if (flags < 0) goto close_and_error;
      if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) goto close_and_error;
    }
    if (cloexec) {
      flags = fcntl(fd, F_GETFD, 0);
      if (flags < 0) goto close_and_error;
      if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) goto close_and_error;
    }
  }
  return fd;

close_and_error:
  close(fd);
  return -1;
}

#endif /* GPR_POSIX_SOCKETUTILS */
