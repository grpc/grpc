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

#ifndef GRPC_INTERNAL_CORE_IOMGR_POLLSET_H
#define GRPC_INTERNAL_CORE_IOMGR_POLLSET_H

#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#define GRPC_POLLSET_KICK_BROADCAST ((grpc_pollset_worker *)1)

/* A grpc_pollset is a set of file descriptors that a higher level item is
   interested in. For example:
    - a server will typically keep a pollset containing all connected channels,
      so that it can find new calls to service
    - a completion queue might keep a pollset with an entry for each transport
      that is servicing a call that it's tracking */

#ifdef GPR_POSIX_SOCKET
#include "src/core/iomgr/pollset_posix.h"
#endif

#ifdef GPR_WIN32
#include "src/core/iomgr/pollset_windows.h"
#endif

void grpc_pollset_init(grpc_pollset *pollset);
/* Begin shutting down the pollset, and call closure when done.
 * GRPC_POLLSET_MU(pollset) must be held */
void grpc_pollset_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_closure *closure);
/** Reset the pollset to its initial state (perhaps with some cached objects);
 *  must have been previously shutdown */
void grpc_pollset_reset(grpc_pollset *pollset);
void grpc_pollset_destroy(grpc_pollset *pollset);

/* Do some work on a pollset.
   May involve invoking asynchronous callbacks, or actually polling file
   descriptors.
   Requires GRPC_POLLSET_MU(pollset) locked.
   May unlock GRPC_POLLSET_MU(pollset) during its execution.

   worker is a (platform-specific) handle that can be used to wake up
   from grpc_pollset_work before any events are received and before the timeout
   has expired. It is both initialized and destroyed by grpc_pollset_work.
   Initialization of worker is guaranteed to occur BEFORE the
   GRPC_POLLSET_MU(pollset) is released for the first time by
   grpc_pollset_work, and it is guaranteed that GRPC_POLLSET_MU(pollset) will
   not be released by grpc_pollset_work AFTER worker has been destroyed.

   Tries not to block past deadline.
   May call grpc_closure_list_run on grpc_closure_list, without holding the
   pollset
   lock */
void grpc_pollset_work(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                       grpc_pollset_worker *worker, gpr_timespec now,
                       gpr_timespec deadline);

/* Break one polling thread out of polling work for this pollset.
   If specific_worker is GRPC_POLLSET_KICK_BROADCAST, kick ALL the workers.
   Otherwise, if specific_worker is non-NULL, then kick that worker. */
void grpc_pollset_kick(grpc_pollset *pollset,
                       grpc_pollset_worker *specific_worker);

#endif /* GRPC_INTERNAL_CORE_IOMGR_POLLSET_H */
