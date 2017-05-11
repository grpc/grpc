/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/lib/iomgr/port.h"

#include "src/core/lib/iomgr/is_epollexclusive_available.h"

#ifdef GRPC_LINUX_EPOLL

#include <grpc/support/log.h>

#include <errno.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "src/core/lib/iomgr/sys_epoll_wrapper.h"

/* This polling engine is only relevant on linux kernels supporting epoll() */
bool grpc_is_epollexclusive_available(void) {
  static bool logged_why_not = false;

  int fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd < 0) {
    if (!logged_why_not) {
      gpr_log(GPR_ERROR,
              "epoll_create1 failed with error: %d. Not using epollex polling "
              "engine.",
              fd);
      logged_why_not = true;
    }
    return false;
  }
  int evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evfd < 0) {
    if (!logged_why_not) {
      gpr_log(GPR_ERROR,
              "eventfd failed with error: %d. Not using epollex polling "
              "engine.",
              fd);
      logged_why_not = true;
    }
    close(fd);
    return false;
  }
  struct epoll_event ev = {
      /* choose events that should cause an error on
         EPOLLEXCLUSIVE enabled kernels - specifically the combination of
         EPOLLONESHOT and EPOLLEXCLUSIVE */
      .events = (uint32_t)(EPOLLET | EPOLLIN | EPOLLEXCLUSIVE | EPOLLONESHOT),
      .data.ptr = NULL};
  if (epoll_ctl(fd, EPOLL_CTL_ADD, evfd, &ev) != 0) {
    if (errno != EINVAL) {
      if (!logged_why_not) {
        gpr_log(
            GPR_ERROR,
            "epoll_ctl with EPOLLEXCLUSIVE | EPOLLONESHOT failed with error: "
            "%d. Not using epollex polling engine.",
            errno);
        logged_why_not = true;
      }
      close(fd);
      close(evfd);
      return false;
    }
  } else {
    if (!logged_why_not) {
      gpr_log(GPR_ERROR,
              "epoll_ctl with EPOLLEXCLUSIVE | EPOLLONESHOT succeeded. This is "
              "evidence of no EPOLLEXCLUSIVE support. Not using "
              "epollex polling engine.");
      logged_why_not = true;
    }
    close(fd);
    close(evfd);
    return false;
  }
  close(evfd);
  close(fd);
  return true;
}

#else

bool grpc_is_epollexclusive_available(void) { return false; }

#endif
