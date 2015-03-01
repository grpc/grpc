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
void grpc_pollset_shutdown(grpc_pollset *pollset,
                           void (*shutdown_done)(void *arg),
                           void *shutdown_done_arg);
void grpc_pollset_destroy(grpc_pollset *pollset);


/* Do some work on a pollset.
   May involve invoking asynchronous callbacks, or actually polling file
   descriptors.
   Requires GRPC_POLLSET_MU(pollset) locked.
   May unlock GRPC_POLLSET_MU(pollset) during its execution. */
int grpc_pollset_work(grpc_pollset *pollset, gpr_timespec deadline);

/* Break a pollset out of polling work
   Requires GRPC_POLLSET_MU(pollset) locked. */
void grpc_pollset_kick(grpc_pollset *pollset);

#endif  /* GRPC_INTERNAL_CORE_IOMGR_POLLSET_H */
