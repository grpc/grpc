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
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

/* This implementation is based on a freelist of pipes. */

#define GRPC_MAX_CACHED_PIPES 50
#define GRPC_PIPE_LOW_WATERMARK 25

typedef struct grpc_kick_pipe_info {
  int pipe_read_fd;
  int pipe_write_fd;
  struct grpc_kick_pipe_info *next;
} grpc_kick_pipe_info;

static grpc_kick_pipe_info *pipe_freelist = NULL;
static int pipe_freelist_count = 0;
static gpr_mu pipe_freelist_mu;

static grpc_kick_pipe_info *allocate_pipe(void) {
  grpc_kick_pipe_info *info;
  gpr_mu_lock(&pipe_freelist_mu);
  if (pipe_freelist != NULL) {
    info = pipe_freelist;
    pipe_freelist = pipe_freelist->next;
    --pipe_freelist_count;
  } else {
    int pipefd[2];
    /* TODO(klempner): Make this nonfatal */
    GPR_ASSERT(0 == pipe(pipefd));
    GPR_ASSERT(grpc_set_socket_nonblocking(pipefd[0], 1));
    GPR_ASSERT(grpc_set_socket_nonblocking(pipefd[1], 1));
    info = gpr_malloc(sizeof(*info));
    info->pipe_read_fd = pipefd[0];
    info->pipe_write_fd = pipefd[1];
    info->next = NULL;
  }
  gpr_mu_unlock(&pipe_freelist_mu);
  return info;
}

static void destroy_pipe(void) {
  /* assumes pipe_freelist_mu is held */
  grpc_kick_pipe_info *current = pipe_freelist;
  pipe_freelist = pipe_freelist->next;
  pipe_freelist_count--;
  close(current->pipe_read_fd);
  close(current->pipe_write_fd);
  gpr_free(current);
}

static void free_pipe(grpc_kick_pipe_info *pipe_info) {
  gpr_mu_lock(&pipe_freelist_mu);
  pipe_info->next = pipe_freelist;
  pipe_freelist = pipe_info;
  pipe_freelist_count++;
  if (pipe_freelist_count > GRPC_MAX_CACHED_PIPES) {
    while (pipe_freelist_count > GRPC_PIPE_LOW_WATERMARK) {
      destroy_pipe();
    }
  }
  gpr_mu_unlock(&pipe_freelist_mu);
}

void grpc_pollset_kick_global_init() {
  pipe_freelist = NULL;
  gpr_mu_init(&pipe_freelist_mu);
}

void grpc_pollset_kick_global_destroy() {
  while (pipe_freelist != NULL) {
    destroy_pipe();
  }
  gpr_mu_destroy(&pipe_freelist_mu);
}

void grpc_pollset_kick_init(grpc_pollset_kick_state *kick_state) {
  gpr_mu_init(&kick_state->mu);
  kick_state->kicked = 0;
  kick_state->pipe_info = NULL;
}

void grpc_pollset_kick_destroy(grpc_pollset_kick_state *kick_state) {
  gpr_mu_destroy(&kick_state->mu);
  GPR_ASSERT(kick_state->pipe_info == NULL);
}

int grpc_pollset_kick_pre_poll(grpc_pollset_kick_state *kick_state) {
  gpr_mu_lock(&kick_state->mu);
  if (kick_state->kicked) {
    kick_state->kicked = 0;
    gpr_mu_unlock(&kick_state->mu);
    return -1;
  }
  kick_state->pipe_info = allocate_pipe();
  gpr_mu_unlock(&kick_state->mu);
  return kick_state->pipe_info->pipe_read_fd;
}

void grpc_pollset_kick_consume(grpc_pollset_kick_state *kick_state) {
  char buf[128];
  int r;

  for (;;) {
    r = read(kick_state->pipe_info->pipe_read_fd, buf, sizeof(buf));
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

void grpc_pollset_kick_post_poll(grpc_pollset_kick_state *kick_state) {
  gpr_mu_lock(&kick_state->mu);
  free_pipe(kick_state->pipe_info);
  kick_state->pipe_info = NULL;
  gpr_mu_unlock(&kick_state->mu);
}

void grpc_pollset_kick_kick(grpc_pollset_kick_state *kick_state) {
  gpr_mu_lock(&kick_state->mu);
  if (kick_state->pipe_info != NULL) {
    char c = 0;
    while (write(kick_state->pipe_info->pipe_write_fd, &c, 1) != 1 &&
           errno == EINTR)
      ;
  } else {
    kick_state->kicked = 1;
  }
  gpr_mu_unlock(&kick_state->mu);
}

#endif
