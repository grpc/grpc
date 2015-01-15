/*
 *
 * Copyright 2014, Google Inc.
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

#ifndef __GRPC_INTERNAL_IOMGR_POLLSET_POSIX_H_
#define __GRPC_INTERNAL_IOMGR_POLLSET_POSIX_H_

#include <grpc/support/sync.h>

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
  gpr_cv cv;
  int counter;
  union {
    int fd;
    void *ptr;
  } data;
} grpc_pollset;

struct grpc_pollset_vtable {
  void (*add_fd)(grpc_pollset *pollset, struct grpc_fd *fd);
  void (*del_fd)(grpc_pollset *pollset, struct grpc_fd *fd);
  int (*maybe_work)(grpc_pollset *pollset, gpr_timespec deadline,
                    gpr_timespec now, int allow_synchronous_callback);
  void (*destroy)(grpc_pollset *pollset);
};

#define GRPC_POLLSET_MU(pollset) (&(pollset)->mu)
#define GRPC_POLLSET_CV(pollset) (&(pollset)->cv)

/* Add an fd to a pollset */
void grpc_pollset_add_fd(grpc_pollset *pollset, struct grpc_fd *fd);
/* Force remove an fd from a pollset (normally they are removed on the next
   poll after an fd is orphaned) */
void grpc_pollset_del_fd(grpc_pollset *pollset, struct grpc_fd *fd);

/* Force any current pollers to break polling */
void grpc_pollset_force_kick(grpc_pollset *pollset);
/* Returns the fd to listen on for kicks */
int grpc_kick_read_fd(grpc_pollset *p);
/* Call after polling has been kicked to leave the kicked state */
void grpc_kick_drain(grpc_pollset *p);

/* All fds get added to a backup pollset to ensure that progress is made
   regardless of applications listening to events. Relying on this is slow
   however (the backup pollset only listens every 100ms or so) - so it's not
   to be relied on. */
grpc_pollset *grpc_backup_pollset(void);

/* turn a pollset into a multipoller: platform specific */
void grpc_platform_become_multipoller(grpc_pollset *pollset,
                                      struct grpc_fd **fds, size_t fd_count);

#endif /* __GRPC_INTERNAL_IOMGR_POLLSET_POSIX_H_ */
