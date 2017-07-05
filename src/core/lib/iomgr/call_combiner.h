/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_IOMGR_CALL_COMBINER_H
#define GRPC_CORE_LIB_IOMGR_CALL_COMBINER_H

#include <stddef.h>

#include <grpc/support/atm.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/support/mpscq.h"

// A simple, lock-free mechanism for serializing activity related to a
// single call.  This is similar to a combiner but is more lightweight.
//
// It requires the callback (or, in the common case where the callback
// actually kicks off a chain of callbacks, the last callback in that
// chain) to explicitly indicate (by calling grpc_call_combiner_stop())
// when it is done with the action that was kicked off by the original
// callback.

typedef struct {
  gpr_atm size;  // size_t, num closures in queue or currently executing
  gpr_mpscq queue;
  grpc_closure_scheduler scheduler;
  grpc_closure* notify_on_cancel;
} grpc_call_combiner;

// Assumes memory was initialized to zero.
void grpc_call_combiner_init(grpc_call_combiner* call_combiner);

void grpc_call_combiner_destroy(grpc_call_combiner* call_combiner);

/// Start processing \a closure on \a call_combiner.
void grpc_call_combiner_start(grpc_exec_ctx* exec_ctx,
                              grpc_call_combiner* call_combiner,
                              grpc_closure* closure, grpc_error* error);
/// Invoked by the callback to indicate that it is done processing.
void grpc_call_combiner_stop(grpc_exec_ctx* exec_ctx,
                             grpc_call_combiner* call_combiner);

/// Tells \a call_combiner to invoke \a closure when
/// grpc_call_combiner_cancel() is called.  If \a closure is NULL, then
/// no closure will be invoked on cancellation.
/// Note: Caller must hold call_combiner before calling this.
void grpc_call_combiner_set_notify_on_cancel(grpc_call_combiner* call_combiner,
                                             grpc_closure* closure);

/// Indicates that the call has been cancelled.
void grpc_call_combiner_cancel(grpc_exec_ctx* exec_ctx,
                               grpc_call_combiner* call_combiner,
                               grpc_error* error);

#endif /* GRPC_CORE_LIB_IOMGR_CALL_COMBINER_H */
