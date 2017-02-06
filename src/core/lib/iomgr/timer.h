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

#ifndef GRPC_CORE_LIB_IOMGR_TIMER_H
#define GRPC_CORE_LIB_IOMGR_TIMER_H

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_UV
#include "src/core/lib/iomgr/timer_uv.h"
#else
#include "src/core/lib/iomgr/timer_generic.h"
#endif /* GRPC_UV */

#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr.h"

typedef struct grpc_timer grpc_timer;

/* Initialize *timer. When expired or canceled, closure will be called with
   error set to indicate if it expired (GRPC_ERROR_NONE) or was canceled
   (GRPC_ERROR_CANCELLED). timer_cb is guaranteed to be called exactly once, and
   application code should check the error to determine how it was invoked. The
   application callback is also responsible for maintaining information about
   when to free up any user-level state. */
void grpc_timer_init(grpc_exec_ctx *exec_ctx, grpc_timer *timer,
                     gpr_timespec deadline, grpc_closure *closure,
                     gpr_timespec now);

/* Note that there is no timer destroy function. This is because the
   timer is a one-time occurrence with a guarantee that the callback will
   be called exactly once, either at expiration or cancellation. Thus, all
   the internal timer event management state is destroyed just before
   that callback is invoked. If the user has additional state associated with
   the timer, the user is responsible for determining when it is safe to
   destroy that state. */

/* Cancel an *timer.
   There are three cases:
   1. We normally cancel the timer
   2. The timer has already run
   3. We can't cancel the timer because it is "in flight".

   In all of these cases, the cancellation is still considered successful.
   They are essentially distinguished in that the timer_cb will be run
   exactly once from either the cancellation (with error GRPC_ERROR_CANCELLED)
   or from the activation (with error GRPC_ERROR_NONE).

   Note carefully that the callback function MAY occur in the same callstack
   as grpc_timer_cancel. It's expected that most timers will be cancelled (their
   primary use is to implement deadlines), and so this code is optimized such
   that cancellation costs as little as possible. Making callbacks run inline
   matches this aim.

   Requires: cancel() must happen after init() on a given timer */
void grpc_timer_cancel(grpc_exec_ctx *exec_ctx, grpc_timer *timer);

/* iomgr internal api for dealing with timers */

/* Check for timers to be run, and run them.
   Return true if timer callbacks were executed.
   If next is non-null, TRY to update *next with the next running timer
   IF that timer occurs before *next current value.
   *next is never guaranteed to be updated on any given execution; however,
   with high probability at least one thread in the system will see an update
   at any time slice. */
bool grpc_timer_check(grpc_exec_ctx *exec_ctx, gpr_timespec now,
                      gpr_timespec *next);
void grpc_timer_list_init(gpr_timespec now);
void grpc_timer_list_shutdown(grpc_exec_ctx *exec_ctx);

/* the following must be implemented by each iomgr implementation */

void grpc_kick_poller(void);

#endif /* GRPC_CORE_LIB_IOMGR_TIMER_H */
