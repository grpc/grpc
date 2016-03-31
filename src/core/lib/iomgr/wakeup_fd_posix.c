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

#include <stddef.h>
#include "src/core/lib/iomgr/wakeup_fd_pipe.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"

static const grpc_wakeup_fd_vtable *wakeup_fd_vtable = NULL;
int grpc_allow_specialized_wakeup_fd = 1;

void grpc_wakeup_fd_global_init(void) {
  if (grpc_allow_specialized_wakeup_fd &&
      grpc_specialized_wakeup_fd_vtable.check_availability()) {
    wakeup_fd_vtable = &grpc_specialized_wakeup_fd_vtable;
  } else {
    wakeup_fd_vtable = &grpc_pipe_wakeup_fd_vtable;
  }
}

void grpc_wakeup_fd_global_destroy(void) { wakeup_fd_vtable = NULL; }

void grpc_wakeup_fd_init(grpc_wakeup_fd *fd_info) {
  wakeup_fd_vtable->init(fd_info);
}

void grpc_wakeup_fd_consume_wakeup(grpc_wakeup_fd *fd_info) {
  wakeup_fd_vtable->consume(fd_info);
}

void grpc_wakeup_fd_wakeup(grpc_wakeup_fd *fd_info) {
  wakeup_fd_vtable->wakeup(fd_info);
}

void grpc_wakeup_fd_destroy(grpc_wakeup_fd *fd_info) {
  wakeup_fd_vtable->destroy(fd_info);
}

#endif /* GPR_POSIX_WAKEUP_FD */
