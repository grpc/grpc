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

#include <poll.h>

#include <grpc/support/sync.h>
#include "src/core/iomgr/exec_ctx.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/iomgr/wakeup_fd_posix.h"

typedef struct grpc_pollset_vtable grpc_pollset_vtable;

/* forward declare only in this file to avoid leaking impl details via
   pollset.h; real users of grpc_fd should always include 'fd_posix.h' and not
   use the struct tag */
struct grpc_fd;

typedef struct grpc_pollset_worker {
  grpc_wakeup_fd wakeup_fd;
  int reevaluate_polling_on_wakeup;
  int kicked_specifically;
  struct grpc_pollset_worker *next;
  struct grpc_pollset_worker *prev;
} grpc_pollset_worker;

typedef struct grpc_pollset {
  /* pollsets under posix can mutate representation as fds are added and
     removed.
     For example, we may choose a poll() based implementation on linux for
     few fds, and an epoll() based implementation for many fds */
  const grpc_pollset_vtable *vtable;
  gpr_mu mu;
  grpc_pollset_worker root_worker;
  int in_flight_cbs;
  int shutting_down;
  int called_shutdown;
  int kicked_without_pollers;
  grpc_closure *shutdown_done;
  grpc_closure_list idle_jobs;
  union {
    int fd;
    void *ptr;
  } data;
} grpc_pollset;

struct grpc_pollset_vtable {
  void (*add_fd)(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                 struct grpc_fd *fd, int and_unlock_pollset);
  void (*del_fd)(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                 struct grpc_fd *fd, int and_unlock_pollset);
  void (*maybe_work_and_unlock)(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                                grpc_pollset_worker *worker,
                                gpr_timespec deadline, gpr_timespec now);
  void (*finish_shutdown)(grpc_pollset *pollset);
  void (*destroy)(grpc_pollset *pollset);
};

#define GRPC_POLLSET_MU(pollset) (&(pollset)->mu)

/* Add an fd to a pollset */
void grpc_pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                         struct grpc_fd *fd);
/* Force remove an fd from a pollset (normally they are removed on the next
   poll after an fd is orphaned) */
void grpc_pollset_del_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                         struct grpc_fd *fd);

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
int grpc_poll_deadline_to_millis_timeout(gpr_timespec deadline,
                                         gpr_timespec now);

/* Allow kick to wakeup the currently polling worker */
#define GRPC_POLLSET_CAN_KICK_SELF 1
/* Force the wakee to repoll when awoken */
#define GRPC_POLLSET_REEVALUATE_POLLING_ON_WAKEUP 2
/* As per grpc_pollset_kick, with an extended set of flags (defined above)
   -- mostly for fd_posix's use. */
void grpc_pollset_kick_ext(grpc_pollset *p,
                           grpc_pollset_worker *specific_worker,
                           gpr_uint32 flags);

/* turn a pollset into a multipoller: platform specific */
typedef void (*grpc_platform_become_multipoller_type)(grpc_exec_ctx *exec_ctx,
                                                      grpc_pollset *pollset,
                                                      struct grpc_fd **fds,
                                                      size_t fd_count);
extern grpc_platform_become_multipoller_type grpc_platform_become_multipoller;

void grpc_poll_become_multipoller(grpc_exec_ctx *exec_ctx,
                                  grpc_pollset *pollset, struct grpc_fd **fds,
                                  size_t fd_count);

/* Return 1 if the pollset has active threads in grpc_pollset_work (pollset must
 * be locked) */
int grpc_pollset_has_workers(grpc_pollset *pollset);

/* override to allow tests to hook poll() usage */
typedef int (*grpc_poll_function_type)(struct pollfd *, nfds_t, int);
extern grpc_poll_function_type grpc_poll_function;
extern grpc_wakeup_fd grpc_global_wakeup_fd;

#endif /* GRPC_INTERNAL_CORE_IOMGR_POLLSET_POSIX_H */
