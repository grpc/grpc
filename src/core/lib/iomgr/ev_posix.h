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

#ifndef GRPC_CORE_LIB_IOMGR_EV_POSIX_H
#define GRPC_CORE_LIB_IOMGR_EV_POSIX_H

#include <poll.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"

typedef struct grpc_fd grpc_fd;

typedef struct grpc_event_engine_vtable {
  size_t pollset_size;

  grpc_fd *(*fd_create)(int fd, const char *name);
  int (*fd_wrapped_fd)(grpc_fd *fd);
  void (*fd_orphan)(grpc_exec_ctx *exec_ctx, grpc_fd *fd, grpc_closure *on_done,
                    int *release_fd, const char *reason);
  void (*fd_shutdown)(grpc_exec_ctx *exec_ctx, grpc_fd *fd);
  void (*fd_notify_on_read)(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                            grpc_closure *closure);
  void (*fd_notify_on_write)(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                             grpc_closure *closure);
  bool (*fd_is_shutdown)(grpc_fd *fd);
  grpc_workqueue *(*fd_get_workqueue)(grpc_fd *fd);
  grpc_pollset *(*fd_get_read_notifier_pollset)(grpc_exec_ctx *exec_ctx,
                                                grpc_fd *fd);

  void (*pollset_init)(grpc_pollset *pollset, gpr_mu **mu);
  void (*pollset_shutdown)(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_closure *closure);
  void (*pollset_reset)(grpc_pollset *pollset);
  void (*pollset_destroy)(grpc_pollset *pollset);
  grpc_error *(*pollset_work)(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                              grpc_pollset_worker **worker, gpr_timespec now,
                              gpr_timespec deadline);
  grpc_error *(*pollset_kick)(grpc_pollset *pollset,
                              grpc_pollset_worker *specific_worker);
  void (*pollset_add_fd)(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                         struct grpc_fd *fd);

  grpc_pollset_set *(*pollset_set_create)(void);
  void (*pollset_set_destroy)(grpc_pollset_set *pollset_set);
  void (*pollset_set_add_pollset)(grpc_exec_ctx *exec_ctx,
                                  grpc_pollset_set *pollset_set,
                                  grpc_pollset *pollset);
  void (*pollset_set_del_pollset)(grpc_exec_ctx *exec_ctx,
                                  grpc_pollset_set *pollset_set,
                                  grpc_pollset *pollset);
  void (*pollset_set_add_pollset_set)(grpc_exec_ctx *exec_ctx,
                                      grpc_pollset_set *bag,
                                      grpc_pollset_set *item);
  void (*pollset_set_del_pollset_set)(grpc_exec_ctx *exec_ctx,
                                      grpc_pollset_set *bag,
                                      grpc_pollset_set *item);
  void (*pollset_set_add_fd)(grpc_exec_ctx *exec_ctx,
                             grpc_pollset_set *pollset_set, grpc_fd *fd);
  void (*pollset_set_del_fd)(grpc_exec_ctx *exec_ctx,
                             grpc_pollset_set *pollset_set, grpc_fd *fd);

  grpc_error *(*kick_poller)(void);

  void (*shutdown_engine)(void);
} grpc_event_engine_vtable;

void grpc_event_engine_init(void);
void grpc_event_engine_shutdown(void);

/* Return the name of the poll strategy */
const char *grpc_get_poll_strategy_name();

/* Create a wrapped file descriptor.
   Requires fd is a non-blocking file descriptor.
   This takes ownership of closing fd. */
grpc_fd *grpc_fd_create(int fd, const char *name);

/* Get a workqueue that's associated with this fd */
grpc_workqueue *grpc_fd_get_workqueue(grpc_fd *fd);

/* Return the wrapped fd, or -1 if it has been released or closed. */
int grpc_fd_wrapped_fd(grpc_fd *fd);

/* Releases fd to be asynchronously destroyed.
   on_done is called when the underlying file descriptor is definitely close()d.
   If on_done is NULL, no callback will be made.
   If release_fd is not NULL, it's set to fd and fd will not be closed.
   Requires: *fd initialized; no outstanding notify_on_read or
   notify_on_write.
   MUST NOT be called with a pollset lock taken */
void grpc_fd_orphan(grpc_exec_ctx *exec_ctx, grpc_fd *fd, grpc_closure *on_done,
                    int *release_fd, const char *reason);

/* Has grpc_fd_shutdown been called on an fd? */
bool grpc_fd_is_shutdown(grpc_fd *fd);

/* Cause any current and future callbacks to fail. */
void grpc_fd_shutdown(grpc_exec_ctx *exec_ctx, grpc_fd *fd);

/* Register read interest, causing read_cb to be called once when fd becomes
   readable, on deadline specified by deadline, or on shutdown triggered by
   grpc_fd_shutdown.
   read_cb will be called with read_cb_arg when *fd becomes readable.
   read_cb is Called with status of GRPC_CALLBACK_SUCCESS if readable,
   GRPC_CALLBACK_TIMED_OUT if the call timed out,
   and CANCELLED if the call was cancelled.

   Requires:This method must not be called before the read_cb for any previous
   call runs. Edge triggered events are used whenever they are supported by the
   underlying platform. This means that users must drain fd in read_cb before
   calling notify_on_read again. Users are also expected to handle spurious
   events, i.e read_cb is called while nothing can be readable from fd  */
void grpc_fd_notify_on_read(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                            grpc_closure *closure);

/* Exactly the same semantics as above, except based on writable events.  */
void grpc_fd_notify_on_write(grpc_exec_ctx *exec_ctx, grpc_fd *fd,
                             grpc_closure *closure);

/* Return the read notifier pollset from the fd */
grpc_pollset *grpc_fd_get_read_notifier_pollset(grpc_exec_ctx *exec_ctx,
                                                grpc_fd *fd);

/* pollset_posix functions */

/* Add an fd to a pollset */
void grpc_pollset_add_fd(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                         struct grpc_fd *fd);

/* pollset_set_posix functions */

void grpc_pollset_set_add_fd(grpc_exec_ctx *exec_ctx,
                             grpc_pollset_set *pollset_set, grpc_fd *fd);
void grpc_pollset_set_del_fd(grpc_exec_ctx *exec_ctx,
                             grpc_pollset_set *pollset_set, grpc_fd *fd);

/* override to allow tests to hook poll() usage */
typedef int (*grpc_poll_function_type)(struct pollfd *, nfds_t, int);
extern grpc_poll_function_type grpc_poll_function;
extern grpc_wakeup_fd grpc_global_wakeup_fd;

#endif /* GRPC_CORE_LIB_IOMGR_EV_POSIX_H */
