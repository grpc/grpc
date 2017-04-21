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

#ifndef GRPC_CORE_LIB_IOMGR_POLLSET_H
#define GRPC_CORE_LIB_IOMGR_POLLSET_H

#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/exec_ctx.h"

#define GRPC_POLLSET_KICK_BROADCAST ((grpc_pollset_worker *)1)

/* A grpc_pollset is a set of file descriptors that a higher level item is
   interested in. For example:
    - a server will typically keep a pollset containing all connected channels,
      so that it can find new calls to service
    - a completion queue might keep a pollset with an entry for each transport
      that is servicing a call that it's tracking */

typedef struct grpc_pollset grpc_pollset;
typedef struct grpc_pollset_worker grpc_pollset_worker;

size_t grpc_pollset_size(void);
/* Initialize a pollset: assumes *pollset contains all zeros */
void grpc_pollset_init(grpc_pollset *pollset, gpr_mu **mu);
/* Begin shutting down the pollset, and call closure when done.
 * pollset's mutex must be held */
void grpc_pollset_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_closure *closure);
void grpc_pollset_destroy(grpc_pollset *pollset);

/* Do some work on a pollset.
   May involve invoking asynchronous callbacks, or actually polling file
   descriptors.
   Requires pollset's mutex locked.
   May unlock its mutex during its execution.

   worker is a (platform-specific) handle that can be used to wake up
   from grpc_pollset_work before any events are received and before the timeout
   has expired. It is both initialized and destroyed by grpc_pollset_work.
   Initialization of worker is guaranteed to occur BEFORE the
   pollset's mutex is released for the first time by grpc_pollset_work
   and it is guaranteed that it will not be released by grpc_pollset_work
   AFTER worker has been destroyed.

   It's legal for worker to be NULL: in that case, this specific thread can not
   be directly woken with a kick, but maybe be indirectly (with a kick against
   the pollset as a whole).

   Tries not to block past deadline.
   May call grpc_closure_list_run on grpc_closure_list, without holding the
   pollset
   lock */
grpc_error *grpc_pollset_work(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                              grpc_pollset_worker **worker, gpr_timespec now,
                              gpr_timespec deadline) GRPC_MUST_USE_RESULT;

/* Break one polling thread out of polling work for this pollset.
   If specific_worker is GRPC_POLLSET_KICK_BROADCAST, kick ALL the workers.
   Otherwise, if specific_worker is non-NULL, then kick that worker. */
grpc_error *grpc_pollset_kick(grpc_pollset *pollset,
                              grpc_pollset_worker *specific_worker)
    GRPC_MUST_USE_RESULT;

#endif /* GRPC_CORE_LIB_IOMGR_POLLSET_H */
