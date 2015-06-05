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

#ifdef GPR_POSIX_SOCKET
#include "src/core/iomgr/pollset_kick_posix.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "src/core/iomgr/socket_utils_posix.h"
#include "src/core/iomgr/wakeup_fd_posix.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

/* This implementation is based on a freelist of wakeup fds, with extra logic to
 * handle kicks while there is no attached fd. */

/* TODO(klempner): Autosize this, and consider providing a way to disable the
 * cap entirely on systems with large fd limits */
#define GRPC_MAX_CACHED_WFDS 50

static grpc_kick_fd_info *fd_freelist = NULL;
static int fd_freelist_count = 0;
static gpr_mu fd_freelist_mu;

static grpc_kick_fd_info *allocate_wfd(void) {
  grpc_kick_fd_info *info = NULL;
  gpr_mu_lock(&fd_freelist_mu);
  if (fd_freelist != NULL) {
    info = fd_freelist;
    fd_freelist = fd_freelist->next;
    --fd_freelist_count;
  }
  gpr_mu_unlock(&fd_freelist_mu);
  if (info == NULL) {
    info = gpr_malloc(sizeof(*info));
    grpc_wakeup_fd_create(&info->wakeup_fd);
    info->next = NULL;
  }
  return info;
}

static void destroy_wfd(grpc_kick_fd_info* wfd) {
  grpc_wakeup_fd_destroy(&wfd->wakeup_fd);
  gpr_free(wfd);
}

static void free_wfd(grpc_kick_fd_info *fd_info) {
  gpr_mu_lock(&fd_freelist_mu);
  if (fd_freelist_count < GRPC_MAX_CACHED_WFDS) {
    fd_info->next = fd_freelist;
    fd_freelist = fd_info;
    fd_freelist_count++;
    fd_info = NULL;
  }
  gpr_mu_unlock(&fd_freelist_mu);

  if (fd_info) {
    destroy_wfd(fd_info);
  }
}

void grpc_pollset_kick_init(grpc_pollset_kick_state *kick_state) {
  gpr_mu_init(&kick_state->mu);
  kick_state->kicked = 0;
  kick_state->fd_list.next = kick_state->fd_list.prev = &kick_state->fd_list;
}

void grpc_pollset_kick_destroy(grpc_pollset_kick_state *kick_state) {
  gpr_mu_destroy(&kick_state->mu);
  GPR_ASSERT(kick_state->fd_list.next == &kick_state->fd_list);
}

grpc_kick_fd_info *grpc_pollset_kick_pre_poll(grpc_pollset_kick_state *kick_state) {
  grpc_kick_fd_info *fd_info;
  gpr_mu_lock(&kick_state->mu);
  if (kick_state->kicked) {
    kick_state->kicked = 0;
    gpr_mu_unlock(&kick_state->mu);
    return NULL;
  }
  fd_info = allocate_wfd();
  fd_info->next = &kick_state->fd_list;
  fd_info->prev = fd_info->next->prev;
  fd_info->next->prev = fd_info->prev->next = fd_info;
  gpr_mu_unlock(&kick_state->mu);
  return fd_info;
}

void grpc_pollset_kick_consume(grpc_pollset_kick_state *kick_state, grpc_kick_fd_info *fd_info) {
  grpc_wakeup_fd_consume_wakeup(&fd_info->wakeup_fd);
}

void grpc_pollset_kick_post_poll(grpc_pollset_kick_state *kick_state, grpc_kick_fd_info *fd_info) {
  gpr_mu_lock(&kick_state->mu);
  fd_info->next->prev = fd_info->prev;
  fd_info->prev->next = fd_info->next;
  free_wfd(fd_info);
  gpr_mu_unlock(&kick_state->mu);
}

void grpc_pollset_kick_kick(grpc_pollset_kick_state *kick_state) {
  gpr_mu_lock(&kick_state->mu);
  if (kick_state->fd_list.next != &kick_state->fd_list) {
    grpc_wakeup_fd_wakeup(&kick_state->fd_list.next->wakeup_fd);
  } else {
    kick_state->kicked = 1;
  }
  gpr_mu_unlock(&kick_state->mu);
}

void grpc_pollset_kick_global_init_fallback_fd(void) {
  gpr_mu_init(&fd_freelist_mu);
  grpc_wakeup_fd_global_init_force_fallback();
}

void grpc_pollset_kick_global_init(void) {
  gpr_mu_init(&fd_freelist_mu);
  grpc_wakeup_fd_global_init();
}

void grpc_pollset_kick_global_destroy(void) {
  while (fd_freelist != NULL) {
    grpc_kick_fd_info *current = fd_freelist;
    fd_freelist = fd_freelist->next;
    destroy_wfd(current);
  }
  grpc_wakeup_fd_global_destroy();
  gpr_mu_destroy(&fd_freelist_mu);
}


#endif  /* GPR_POSIX_SOCKET */
