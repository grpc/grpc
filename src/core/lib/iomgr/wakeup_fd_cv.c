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

#define GRPC_POLLCV_MAX_TABLE_RESIZE 256

extern cv_fd_table g_cvfds[GRPC_POLLCV_TABLE_SHARDS];

// Attempt to allocate a cv fd on a shard.  Returns 1 on success, 0 on
// failure
static int cv_fd_init_on_shard(grpc_wakeup_fd* fd_info, int shard) {
  int newsize;
  int idx;
  gpr_mu_lock(&g_cvfds[shard].mu);
  if (!g_cvfds[shard].free_fds) {
    if (g_cvfds[shard].size == GRPC_POLLCV_MAX_SHARD_SIZE) {
      // No more room on this shard
      gpr_mu_unlock(&g_cvfds[shard].mu);
      return 0;
    }
    newsize = GPR_MIN(g_cvfds[shard].size * 2,
                      g_cvfds[shard].size + GRPC_POLLCV_MAX_TABLE_RESIZE);
    newsize = GPR_MIN(GRPC_POLLCV_MAX_SHARD_SIZE, newsize);
    g_cvfds[shard].cvfds = gpr_realloc(g_cvfds[shard].cvfds,
                                       sizeof(fd_node) * (unsigned int)newsize);
    for (int i = g_cvfds[shard].size; i < newsize; i++) {
      g_cvfds[shard].cvfds[i].is_set = 0;
      g_cvfds[shard].cvfds[i].cvs = NULL;
      g_cvfds[shard].cvfds[i].next_free = g_cvfds[shard].free_fds;
      g_cvfds[shard].free_fds = &g_cvfds[shard].cvfds[i];
    }
    g_cvfds[shard].size = newsize;
  }

  idx = (int)(g_cvfds[shard].free_fds - g_cvfds[shard].cvfds);
  g_cvfds[shard].free_fds = g_cvfds[shard].free_fds->next_free;
  g_cvfds[shard].cvfds[idx].cvs = NULL;
  g_cvfds[shard].cvfds[idx].is_set = 0;
  fd_info->read_fd = GRPC_POLLCV_SHARD_IDX_TO_FD(shard, idx);
  fd_info->write_fd = -1;
  gpr_mu_unlock(&g_cvfds[shard].mu);
  return 1;
}

static grpc_error* cv_fd_init(grpc_wakeup_fd* fd_info) {
  int shard = rand() % GRPC_POLLCV_TABLE_SHARDS;
  if (!cv_fd_init_on_shard(fd_info, shard)) {
    // Our random shard was full, just try all shards
    for (int i = 0; i < GRPC_POLLCV_TABLE_SHARDS; i++) {
      if (cv_fd_init_on_shard(fd_info, shard)) {
        break;
      }
    }
    // Exausted all 32-bit negative numbers with cv wakeup fds
    GPR_ASSERT(0);
  }
  return GRPC_ERROR_NONE;
}

static grpc_error* cv_fd_wakeup(grpc_wakeup_fd* fd_info) {
  cv_node* cvn;
  int shard = GRPC_POLLCV_FD_TO_SHARD(fd_info->read_fd);
  int idx = GRPC_POLLCV_FD_TO_IDX(fd_info->read_fd);
  gpr_mu_lock(&g_cvfds[shard].mu);
  g_cvfds[shard].cvfds[idx].is_set = 1;
  cvn = g_cvfds[shard].cvfds[idx].cvs;
  while (cvn) {
    gpr_mu_lock(cvn->mu);
    gpr_cv_signal(cvn->cv);
    gpr_mu_unlock(cvn->mu);
    cvn = cvn->next;
  }
  gpr_mu_unlock(&g_cvfds[shard].mu);
  return GRPC_ERROR_NONE;
}

static grpc_error* cv_fd_consume(grpc_wakeup_fd* fd_info) {
  int shard = GRPC_POLLCV_FD_TO_SHARD(fd_info->read_fd);
  int idx = GRPC_POLLCV_FD_TO_IDX(fd_info->read_fd);
  gpr_mu_lock(&g_cvfds[shard].mu);
  g_cvfds[shard].cvfds[idx].is_set = 0;
  gpr_mu_unlock(&g_cvfds[shard].mu);
  return GRPC_ERROR_NONE;
}

static void cv_fd_destroy(grpc_wakeup_fd* fd_info) {
  if (fd_info->read_fd == 0) {
    return;
  }
  int shard = GRPC_POLLCV_FD_TO_SHARD(fd_info->read_fd);
  int idx = GRPC_POLLCV_FD_TO_IDX(fd_info->read_fd);
  gpr_mu_lock(&g_cvfds[shard].mu);
  // Assert that there are no active pollers
  GPR_ASSERT(!g_cvfds[shard].cvfds[idx].cvs);
  g_cvfds[shard].cvfds[idx].next_free = g_cvfds[shard].free_fds;
  g_cvfds[shard].free_fds = &g_cvfds[shard].cvfds[idx];
  gpr_mu_unlock(&g_cvfds[shard].mu);
}

static int cv_check_availability(void) { return 1; }

const grpc_wakeup_fd_vtable grpc_cv_wakeup_fd_vtable = {
    cv_fd_init, cv_fd_consume, cv_fd_wakeup, cv_fd_destroy,
    cv_check_availability};

#endif /* GRPC_POSIX_WAKUP_FD */
