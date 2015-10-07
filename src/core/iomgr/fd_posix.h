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

#ifndef GRPC_INTERNAL_CORE_IOMGR_FD_POSIX_H
#define GRPC_INTERNAL_CORE_IOMGR_FD_POSIX_H

#include "src/core/iomgr/iomgr_internal.h"
#include "src/core/iomgr/pollset.h"
#include <grpc/support/atm.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

typedef struct grpc_fd grpc_fd;

typedef struct grpc_fd_watcher {
  struct grpc_fd_watcher *next;
  struct grpc_fd_watcher *prev;
  grpc_pollset *pollset;
  grpc_pollset_worker *worker;
  grpc_fd *fd;
} grpc_fd_watcher;

struct grpc_fd {
  int fd;
  /* refst format:
     bit0:   1=active/0=orphaned
     bit1-n: refcount
     meaning that mostly we ref by two to avoid altering the orphaned bit,
     and just unref by 1 when we're ready to flag the object as orphaned */
  gpr_atm refst;

  gpr_mu mu;
  int shutdown;
  int closed;

  /* The watcher list.

     The following watcher related fields are protected by watcher_mu.

     An fd_watcher is an ephemeral object created when an fd wants to
     begin polling, and destroyed after the poll.

     It denotes the fd's interest in whether to read poll or write poll
     or both or neither on this fd.

     If a watcher is asked to poll for reads or writes, the read_watcher
     or write_watcher fields are set respectively. A watcher may be asked
     to poll for both, in which case both fields will be set.

     read_watcher and write_watcher may be NULL if no watcher has been
     asked to poll for reads or writes.

     If an fd_watcher is not asked to poll for reads or writes, it's added
     to a linked list of inactive watchers, rooted at inactive_watcher_root.
     If at a later time there becomes need of a poller to poll, one of
     the inactive pollers may be kicked out of their poll loops to take
     that responsibility. */
  grpc_fd_watcher inactive_watcher_root;
  grpc_fd_watcher *read_watcher;
  grpc_fd_watcher *write_watcher;

  grpc_closure *read_closure;
  grpc_closure *write_closure;

  struct grpc_fd *freelist_next;

  grpc_closure *on_done_closure;

  grpc_iomgr_object iomgr_object;
};

/* Create a wrapped file descriptor.
   Requires fd is a non-blocking file descriptor.
   This takes ownership of closing fd. */
grpc_fd *grpc_fd_create(int fd, const char *name);

/* Releases fd to be asynchronously destroyed.
   on_done is called when the underlying file descriptor is definitely close()d.
   If on_done is NULL, no callback will be made.
   Requires: *fd initialized; no outstanding notify_on_read or
   notify_on_write.
   MUST NOT be called with a pollset lock taken */
void grpc_fd_orphan(grpc_exec_ctx *exec_ctx, grpc_fd *fd, grpc_closure *on_done,
                    const char *reason);

/* Begin polling on an fd.
   Registers that the given pollset is interested in this fd - so that if read
   or writability interest changes, the pollset can be kicked to pick up that
   new interest.
   Return value is:
     (fd_needs_read? read_mask : 0) | (fd_needs_write? write_mask : 0)
   i.e. a combination of read_mask and write_mask determined by the fd's current
   interest in said events.
   Polling strategies that do not need to alter their behavior depending on the
   fd's current interest (such as epoll) do not need to call this function.
   MUST NOT be called with a pollset lock taken */
gpr_uint32 grpc_fd_begin_poll(grpc_fd *fd, grpc_pollset *pollset,
                              grpc_pollset_worker *worker, gpr_uint32 read_mask,
                              gpr_uint32 write_mask, grpc_fd_watcher *rec);
/* Complete polling previously started with grpc_fd_begin_poll
   MUST NOT be called with a pollset lock taken
   if got_read or got_write are 1, also does the become_{readable,writable} as
   appropriate. */
void grpc_fd_end_poll(grpc_exec_ctx *exec_ctx, grpc_fd_watcher *rec,
                      int got_read, int got_write);

/* Return 1 if this fd is orphaned, 0 otherwise */
int grpc_fd_is_orphaned(grpc_fd *fd);

/* Cause any current callbacks to error out with GRPC_CALLBACK_CANCELLED. */
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

/* Notification from the poller to an fd that it has become readable or
   writable.
   If allow_synchronous_callback is 1, allow running the fd callback inline
   in this callstack, otherwise register an asynchronous callback and return */
void grpc_fd_become_readable(grpc_exec_ctx *exec_ctx, grpc_fd *fd);
void grpc_fd_become_writable(grpc_exec_ctx *exec_ctx, grpc_fd *fd);

/* Reference counting for fds */
#ifdef GRPC_FD_REF_COUNT_DEBUG
void grpc_fd_ref(grpc_fd *fd, const char *reason, const char *file, int line);
void grpc_fd_unref(grpc_fd *fd, const char *reason, const char *file, int line);
#define GRPC_FD_REF(fd, reason) grpc_fd_ref(fd, reason, __FILE__, __LINE__)
#define GRPC_FD_UNREF(fd, reason) grpc_fd_unref(fd, reason, __FILE__, __LINE__)
#else
void grpc_fd_ref(grpc_fd *fd);
void grpc_fd_unref(grpc_fd *fd);
#define GRPC_FD_REF(fd, reason) grpc_fd_ref(fd)
#define GRPC_FD_UNREF(fd, reason) grpc_fd_unref(fd)
#endif

void grpc_fd_global_init(void);
void grpc_fd_global_shutdown(void);

#endif /* GRPC_INTERNAL_CORE_IOMGR_FD_POSIX_H */
