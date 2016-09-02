/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPC_CORE_LIB_IOMGR_COMBINER_H
#define GRPC_CORE_LIB_IOMGR_COMBINER_H

#include <stddef.h>

#include <grpc/support/atm.h>
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/support/mpscq.h"

// Provides serialized access to some resource.
// Each action queued on a combiner is executed serially in a borrowed thread.
// The actual thread executing actions may change over time (but there will only
// every be one at a time).

// Initialize the lock, with an optional workqueue to shift load to when
// necessary
grpc_combiner *grpc_combiner_create(grpc_workqueue *optional_workqueue);
// Destroy the lock
void grpc_combiner_destroy(grpc_exec_ctx *exec_ctx, grpc_combiner *lock);
// Execute \a action within the lock.
void grpc_combiner_execute(grpc_exec_ctx *exec_ctx, grpc_combiner *lock,
                           grpc_closure *closure, grpc_error *error);
// Execute \a action within the lock just prior to unlocking.
// if \a hint_async_break is true, the combiner tries to hand execution to
// another thread before finishing the primary queue of combined closures and
// executing the finally list.
// Deprecation warning: \a hint_async_break will be removed in a future version
// Takes a very slow and round-about path if not called from a
// grpc_combiner_execute closure.
void grpc_combiner_execute_finally(grpc_exec_ctx *exec_ctx, grpc_combiner *lock,
                                   grpc_closure *closure, grpc_error *error,
                                   bool hint_async_break);
// Deprecated: force the finally list execution onto another thread
void grpc_combiner_force_async_finally(grpc_combiner *lock);

extern int grpc_combiner_trace;

#endif /* GRPC_CORE_LIB_IOMGR_COMBINER_H */
