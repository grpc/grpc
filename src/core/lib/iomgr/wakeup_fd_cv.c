/*
 *
 * Copyright 2016, Google Inc.
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

#ifdef GRPC_POSIX_WAKEUP_FD

#include "src/core/lib/iomgr/wakeup_fd_cv.h"

#include <errno.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#define MAX_TABLE_RESIZE 256

extern cv_fd_table g_cvfds;

static grpc_error* cv_fd_init(grpc_wakeup_fd* fd_info) {
  unsigned int i, newsize;
  int idx;
  gpr_mu_lock(&g_cvfds.mu);
  if (!g_cvfds.free_fds) {
    newsize = GPR_MIN(g_cvfds.size * 2, g_cvfds.size + MAX_TABLE_RESIZE);
    g_cvfds.cvfds = gpr_realloc(g_cvfds.cvfds, sizeof(fd_node) * newsize);
    for (i = g_cvfds.size; i < newsize; i++) {
      g_cvfds.cvfds[i].is_set = 0;
      g_cvfds.cvfds[i].cvs = NULL;
      g_cvfds.cvfds[i].next_free = g_cvfds.free_fds;
      g_cvfds.free_fds = &g_cvfds.cvfds[i];
    }
    g_cvfds.size = newsize;
  }

  idx = (int)(g_cvfds.free_fds - g_cvfds.cvfds);
  g_cvfds.free_fds = g_cvfds.free_fds->next_free;
  g_cvfds.cvfds[idx].cvs = NULL;
  g_cvfds.cvfds[idx].is_set = 0;
  fd_info->read_fd = IDX_TO_FD(idx);
  fd_info->write_fd = -1;
  gpr_mu_unlock(&g_cvfds.mu);
  return GRPC_ERROR_NONE;
}

static grpc_error* cv_fd_wakeup(grpc_wakeup_fd* fd_info) {
  cv_node* cvn;
  gpr_mu_lock(&g_cvfds.mu);
  g_cvfds.cvfds[FD_TO_IDX(fd_info->read_fd)].is_set = 1;
  cvn = g_cvfds.cvfds[FD_TO_IDX(fd_info->read_fd)].cvs;
  while (cvn) {
    gpr_cv_signal(cvn->cv);
    cvn = cvn->next;
  }
  gpr_mu_unlock(&g_cvfds.mu);
  return GRPC_ERROR_NONE;
}

static grpc_error* cv_fd_consume(grpc_wakeup_fd* fd_info) {
  gpr_mu_lock(&g_cvfds.mu);
  g_cvfds.cvfds[FD_TO_IDX(fd_info->read_fd)].is_set = 0;
  gpr_mu_unlock(&g_cvfds.mu);
  return GRPC_ERROR_NONE;
}

static void cv_fd_destroy(grpc_wakeup_fd* fd_info) {
  if (fd_info->read_fd == 0) {
    return;
  }
  gpr_mu_lock(&g_cvfds.mu);
  // Assert that there are no active pollers
  GPR_ASSERT(!g_cvfds.cvfds[FD_TO_IDX(fd_info->read_fd)].cvs);
  g_cvfds.cvfds[FD_TO_IDX(fd_info->read_fd)].next_free = g_cvfds.free_fds;
  g_cvfds.free_fds = &g_cvfds.cvfds[FD_TO_IDX(fd_info->read_fd)];
  gpr_mu_unlock(&g_cvfds.mu);
}

static int cv_check_availability(void) { return 1; }

const grpc_wakeup_fd_vtable grpc_cv_wakeup_fd_vtable = {
    cv_fd_init, cv_fd_consume, cv_fd_wakeup, cv_fd_destroy,
    cv_check_availability};

#endif /* GRPC_POSIX_WAKUP_FD */
