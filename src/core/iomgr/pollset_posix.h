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

#ifndef GRPC_INTERNAL_CORE_IOMGR_POLLSET_POSIX_H
#define GRPC_INTERNAL_CORE_IOMGR_POLLSET_POSIX_H

#include <grpc/support/sync.h>

#include "src/core/iomgr/pollset_kick_posix.h"

typedef struct grpc_pollset_vtable grpc_pollset_vtable;

/* forward declare only in this file to avoid leaking impl details via
   pollset.h; real users of grpc_fd should always include 'fd_posix.h' and not
   use the struct tag */
struct grpc_fd;

typedef struct grpc_pollset {
  /* pollsets under posix can mutate representation as fds are added and
     removed.
     For example, we may choose a poll() based implementation on linux for
     few fds, and an epoll() based implementation for many fds */
  const grpc_pollset_vtable *vtable;
  gpr_mu mu;
  grpc_pollset_kick_state kick_state;
  int counter;
  int in_flight_cbs;
  int shutting_down;
  int called_shutdown;
  void (*shutdown_done_cb)(void *arg);
  void *shutdown_done_arg;
  union {
    int fd;
    void *ptr;
  } data;
} grpc_pollset;

struct grpc_pollset_vtable {
  void (*add_fd)(grpc_pollset *pollset, struct grpc_fd *fd,
                 int and_unlock_pollset);
  void (*del_fd)(grpc_pollset *pollset, struct grpc_fd *fd,
                 int and_unlock_pollset);
  void (*maybe_work)(grpc_pollset *pollset, gpr_timespec deadline,
                     gpr_timespec now, int allow_synchronous_callback);
  void (*kick)(grpc_pollset *pollset);
  void (*finish_shutdown)(grpc_pollset *pollset);
  void (*destroy)(grpc_pollset *pollset);
};

#define GRPC_POLLSET_MU(pollset) (&(pollset)->mu)

/* Add an fd to a pollset */
void grpc_pollset_add_fd(grpc_pollset *pollset, struct grpc_fd *fd);
/* Force remove an fd from a pollset (normally they are removed on the next
   poll after an fd is orphaned) */
void grpc_pollset_del_fd(grpc_pollset *pollset, struct grpc_fd *fd);

/* Force any current pollers to break polling: it's the callers responsibility
   to ensure that the pollset indeed needs to be kicked - no verification that
   the pollset is actually performing polling work is done. At worst this will
   result in spurious wakeups if performed at the wrong moment.
   Does not touch pollset->mu. */
void grpc_pollset_force_kick(grpc_pollset *pollset);
/* Returns the fd to listen on for kicks */
int grpc_kick_read_fd(grpc_pollset *p);
/* Call after polling has been kicked to leave the kicked state */
void grpc_kick_drain(grpc_pollset *p);

/* Convert a timespec to milliseconds:
   - very small or negative poll times are clamped to zero to do a 
     non-blocking poll (which becomes spin polling)
   - other small values are rounded up to one millisecond
   - longer than a millisecond polls are rounded up to the next nearest 
     millisecond to avoid spinning
   - infinite timeouts are converted to -1 */
int grpc_poll_deadline_to_millis_timeout(gpr_timespec deadline, gpr_timespec now);

/* turn a pollset into a multipoller: platform specific */
typedef void (*grpc_platform_become_multipoller_type)(grpc_pollset *pollset,
                                                      struct grpc_fd **fds,
                                                      size_t fd_count);
extern grpc_platform_become_multipoller_type grpc_platform_become_multipoller;

void grpc_poll_become_multipoller(grpc_pollset *pollset, struct grpc_fd **fds,
                                  size_t fd_count);

#endif /* GRPC_INTERNAL_CORE_IOMGR_POLLSET_POSIX_H */
