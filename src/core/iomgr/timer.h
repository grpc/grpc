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

#ifndef GRPC_INTERNAL_CORE_IOMGR_TIMER_H
#define GRPC_INTERNAL_CORE_IOMGR_TIMER_H

#include "src/core/iomgr/iomgr.h"
#include "src/core/iomgr/exec_ctx.h"
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

typedef struct grpc_timer {
  gpr_timespec deadline;
  uint32_t heap_index; /* INVALID_HEAP_INDEX if not in heap */
  int triggered;
  struct grpc_timer *next;
  struct grpc_timer *prev;
  grpc_closure closure;
} grpc_timer;

/* Initialize *timer. When expired or canceled, timer_cb will be called with
   *timer_cb_arg and status to indicate if it expired (SUCCESS) or was
   canceled (CANCELLED). timer_cb is guaranteed to be called exactly once,
   and application code should check the status to determine how it was
   invoked. The application callback is also responsible for maintaining
   information about when to free up any user-level state. */
void grpc_timer_init(grpc_exec_ctx *exec_ctx, grpc_timer *timer,
                     gpr_timespec deadline, grpc_iomgr_cb_func timer_cb,
                     void *timer_cb_arg, gpr_timespec now);

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
   exactly once from either the cancellation (with status CANCELLED)
   or from the activation (with status SUCCESS)

   Note carefully that the callback function MAY occur in the same callstack
   as grpc_timer_cancel. It's expected that most timers will be cancelled (their
   primary use is to implement deadlines), and so this code is optimized such
   that cancellation costs as little as possible. Making callbacks run inline
   matches this aim.

   Requires:  cancel() must happen after add() on a given timer */
void grpc_timer_cancel(grpc_exec_ctx *exec_ctx, grpc_timer *timer);

#endif /* GRPC_INTERNAL_CORE_IOMGR_TIMER_H */
