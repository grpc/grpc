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

#include "src/core/iomgr/pollset_kick_posix.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "src/core/iomgr/pollset_kick_eventfd.h"
#include "src/core/iomgr/socket_utils_posix.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

/* This implementation is based on a freelist of pipes. */

#define GRPC_MAX_CACHED_PIPES 50
#define GRPC_PIPE_LOW_WATERMARK 25

static grpc_kick_fd_info *fd_freelist = NULL;
static int fd_freelist_count = 0;
static gpr_mu fd_freelist_mu;
static const grpc_pollset_kick_vtable *kick_vtable = NULL;

static grpc_kick_fd_info *allocate_pipe(void) {
  grpc_kick_fd_info *info;
  gpr_mu_lock(&fd_freelist_mu);
  if (fd_freelist != NULL) {
    info = fd_freelist;
    fd_freelist = fd_freelist->next;
    --fd_freelist_count;
  } else {
    info = gpr_malloc(sizeof(*info));
    kick_vtable->create(info);
    info->next = NULL;
  }
  gpr_mu_unlock(&fd_freelist_mu);
  return info;
}

static void destroy_pipe(void) {
  /* assumes fd_freelist_mu is held */
  grpc_kick_fd_info *current = fd_freelist;
  fd_freelist = fd_freelist->next;
  fd_freelist_count--;
  kick_vtable->destroy(current);
  gpr_free(current);
}

static void free_pipe(grpc_kick_fd_info *fd_info) {
  gpr_mu_lock(&fd_freelist_mu);
  fd_info->next = fd_freelist;
  fd_freelist = fd_info;
  fd_freelist_count++;
  if (fd_freelist_count > GRPC_MAX_CACHED_PIPES) {
    while (fd_freelist_count > GRPC_PIPE_LOW_WATERMARK) {
      destroy_pipe();
    }
  }
  gpr_mu_unlock(&fd_freelist_mu);
}

void grpc_pollset_kick_init(grpc_pollset_kick_state *kick_state) {
  gpr_mu_init(&kick_state->mu);
  kick_state->kicked = 0;
  kick_state->fd_info = NULL;
}

void grpc_pollset_kick_destroy(grpc_pollset_kick_state *kick_state) {
  gpr_mu_destroy(&kick_state->mu);
  GPR_ASSERT(kick_state->fd_info == NULL);
}

int grpc_pollset_kick_pre_poll(grpc_pollset_kick_state *kick_state) {
  gpr_mu_lock(&kick_state->mu);
  if (kick_state->kicked) {
    kick_state->kicked = 0;
    gpr_mu_unlock(&kick_state->mu);
    return -1;
  }
  kick_state->fd_info = allocate_pipe();
  gpr_mu_unlock(&kick_state->mu);
  return kick_state->fd_info->read_fd;
}

void grpc_pollset_kick_consume(grpc_pollset_kick_state *kick_state) {
  kick_vtable->consume(kick_state->fd_info);
}

void grpc_pollset_kick_post_poll(grpc_pollset_kick_state *kick_state) {
  gpr_mu_lock(&kick_state->mu);
  free_pipe(kick_state->fd_info);
  kick_state->fd_info = NULL;
  gpr_mu_unlock(&kick_state->mu);
}

void grpc_pollset_kick_kick(grpc_pollset_kick_state *kick_state) {
  gpr_mu_lock(&kick_state->mu);
  if (kick_state->fd_info != NULL) {
    kick_vtable->kick(kick_state->fd_info);
  } else {
    kick_state->kicked = 1;
  }
  gpr_mu_unlock(&kick_state->mu);
}

static void pipe_create(grpc_kick_fd_info *fd_info) {
  int pipefd[2];
  /* TODO(klempner): Make this nonfatal */
  GPR_ASSERT(0 == pipe(pipefd));
  GPR_ASSERT(grpc_set_socket_nonblocking(pipefd[0], 1));
  GPR_ASSERT(grpc_set_socket_nonblocking(pipefd[1], 1));
  fd_info->read_fd = pipefd[0];
  fd_info->write_fd = pipefd[1];
}

static void pipe_consume(grpc_kick_fd_info *fd_info) {
  char buf[128];
  int r;

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

static void pipe_kick(grpc_kick_fd_info *fd_info) {
  char c = 0;
  while (write(fd_info->write_fd, &c, 1) != 1 && errno == EINTR)
    ;
}

static void pipe_destroy(grpc_kick_fd_info *fd_info) {
  close(fd_info->read_fd);
  close(fd_info->write_fd);
}

static const grpc_pollset_kick_vtable pipe_kick_vtable = {
  pipe_create, pipe_consume, pipe_kick, pipe_destroy
};

static void global_init_common(void) {
  fd_freelist = NULL;
  gpr_mu_init(&fd_freelist_mu);
}

void grpc_pollset_kick_global_init_posix(void) {
  global_init_common();
  kick_vtable = &pipe_kick_vtable;
}

void grpc_pollset_kick_global_init(void) {
  global_init_common();
  kick_vtable = grpc_pollset_kick_eventfd_init();
  if (kick_vtable == NULL) {
    kick_vtable = &pipe_kick_vtable;
  }
}

void grpc_pollset_kick_global_destroy(void) {
  while (fd_freelist != NULL) {
    destroy_pipe();
  }
  gpr_mu_destroy(&fd_freelist_mu);
}


