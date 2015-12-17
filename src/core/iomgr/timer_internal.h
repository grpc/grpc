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

#ifndef GRPC_INTERNAL_CORE_IOMGR_TIMER_INTERNAL_H
#define GRPC_INTERNAL_CORE_IOMGR_TIMER_INTERNAL_H

#include "src/core/iomgr/exec_ctx.h"
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

/* iomgr internal api for dealing with timers */

/* Check for timers to be run, and run them.
   Return non zero if timer callbacks were executed.
   Drops drop_mu if it is non-null before executing callbacks.
   If next is non-null, TRY to update *next with the next running timer
   IF that timer occurs before *next current value.
   *next is never guaranteed to be updated on any given execution; however,
   with high probability at least one thread in the system will see an update
   at any time slice. */

int grpc_timer_check(grpc_exec_ctx* exec_ctx, gpr_timespec now,
                     gpr_timespec* next);
void grpc_timer_list_init(gpr_timespec now);
void grpc_timer_list_shutdown(grpc_exec_ctx* exec_ctx);

/* the following must be implemented by each iomgr implementation */

void grpc_kick_poller(void);

#endif /* GRPC_INTERNAL_CORE_IOMGR_TIMER_INTERNAL_H */
