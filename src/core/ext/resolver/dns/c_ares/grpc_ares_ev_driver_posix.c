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
#include <grpc/support/port_platform.h>
#ifndef GRPC_NATIVE_ADDRESS_RESOLVE
#ifdef GPR_POSIX_SOCKET

#include "src/core/ext/resolver/dns/c_ares/grpc_ares_ev_driver.h"

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/sockaddr.h"

#include <ares.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/support/block_annotate.h"
#include "src/core/lib/support/string.h"

typedef struct fd_pair {
  grpc_fd *grpc_fd;
  int fd;
  struct fd_pair *next;
} fd_pair;

struct grpc_ares_ev_driver {
  bool closing;
  ares_socket_t socks[ARES_GETSOCK_MAXNUM];
  int bitmask;
  grpc_closure driver_closure;
  grpc_pollset_set *pollset_set;
  ares_channel channel;
  fd_pair *fds;
};

grpc_error *grpc_ares_ev_driver_create(grpc_ares_ev_driver **ev_driver,
                                       grpc_pollset_set *pollset_set) {
  int status;
  *ev_driver = gpr_malloc(sizeof(grpc_ares_ev_driver));
  status = ares_init(&(*ev_driver)->channel);
  if (status != ARES_SUCCESS) {
    gpr_free(*ev_driver);
    return GRPC_ERROR_CREATE("Failed to init ares channel");
  }
  (*ev_driver)->pollset_set = pollset_set;
  (*ev_driver)->fds = NULL;
  (*ev_driver)->closing = false;
  return GRPC_ERROR_NONE;
}

void grpc_ares_ev_driver_destroy(grpc_ares_ev_driver *ev_driver) {
  ev_driver->closing = true;
}

static fd_pair *get_fd(fd_pair **head, int fd) {
  fd_pair dummy_head;
  fd_pair *node;
  fd_pair *ret;

  dummy_head.next = *head;
  node = &dummy_head;
  while (node->next != NULL) {
    if (node->next->fd == fd) {
      ret = node->next;
      node->next = node->next->next;
      *head = dummy_head.next;
      return ret;
    }
  }
  return NULL;
}

static void driver_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  grpc_ares_ev_driver *d = arg;
  size_t i;

  if (error == GRPC_ERROR_NONE) {
    for (i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      ares_process_fd(
          d->channel,
          ARES_GETSOCK_READABLE(d->bitmask, i) ? d->socks[i] : ARES_SOCKET_BAD,
          ARES_GETSOCK_WRITABLE(d->bitmask, i) ? d->socks[i] : ARES_SOCKET_BAD);
    }
  } else {
    ares_cancel(d->channel);
  }
  grpc_ares_notify_on_event(exec_ctx, d);
}

ares_channel *grpc_ares_ev_driver_get_channel(grpc_ares_ev_driver *ev_driver) {
  return &ev_driver->channel;
}

void grpc_ares_notify_on_event(grpc_exec_ctx *exec_ctx,
                               grpc_ares_ev_driver *ev_driver) {
  size_t i;
  fd_pair *new_list = NULL;
  if (!ev_driver->closing) {
    ev_driver->bitmask =
        ares_getsock(ev_driver->channel, ev_driver->socks, ARES_GETSOCK_MAXNUM);
    grpc_closure_init(&ev_driver->driver_closure, driver_cb, ev_driver);
    for (i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      char *fd_name;
      gpr_asprintf(&fd_name, "ares_ev_driver-%" PRIuPTR, i);

      if (ARES_GETSOCK_READABLE(ev_driver->bitmask, i) ||
          ARES_GETSOCK_WRITABLE(ev_driver->bitmask, i)) {
        fd_pair *fdp = get_fd(&ev_driver->fds, ev_driver->socks[i]);
        if (!fdp) {
          fdp = gpr_malloc(sizeof(fd_pair));
          fdp->grpc_fd = grpc_fd_create(ev_driver->socks[i], fd_name);
          fdp->fd = ev_driver->socks[i];
          grpc_pollset_set_add_fd(exec_ctx, ev_driver->pollset_set,
                                  fdp->grpc_fd);
        }
        fdp->next = new_list;
        new_list = fdp;

        if (ARES_GETSOCK_READABLE(ev_driver->bitmask, i)) {
          grpc_fd_notify_on_read(exec_ctx, fdp->grpc_fd,
                                 &ev_driver->driver_closure);
        }
        if (ARES_GETSOCK_WRITABLE(ev_driver->bitmask, i)) {
          grpc_fd_notify_on_write(exec_ctx, fdp->grpc_fd,
                                  &ev_driver->driver_closure);
        }
      }
      gpr_free(fd_name);
    }
  }

  while (ev_driver->fds != NULL) {
    fd_pair *cur;

    cur = ev_driver->fds;
    ev_driver->fds = ev_driver->fds->next;
    grpc_pollset_set_del_fd(exec_ctx, ev_driver->pollset_set, cur->grpc_fd);
    grpc_fd_shutdown(exec_ctx, cur->grpc_fd);
    grpc_fd_orphan(exec_ctx, cur->grpc_fd, NULL, NULL, "c-ares query finished");
    gpr_free(cur);
  }

  ev_driver->fds = new_list;
  if (ev_driver->closing) {
    ares_destroy(ev_driver->channel);
    gpr_free(ev_driver);
  }
}

#endif /* GPR_POSIX_SOCKET */
#endif /* GRPC_NATIVE_ADDRESS_RESOLVE */
