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
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/support/mpscq.h"

// Provides serialized access to some resource.
// Each action queued on a combiner is executed serially in a borrowed thread.
// The actual thread executing actions may change over time (but there will only
// every be one at a time).

// Initialize the lock, with an optional workqueue to shift load to when
// necessary
grpc_combiner *grpc_combiner_create(grpc_workqueue *optional_workqueue);

//#define GRPC_COMBINER_REFCOUNT_DEBUG
#ifdef GRPC_COMBINER_REFCOUNT_DEBUG
#define GRPC_COMBINER_DEBUG_ARGS \
  , const char *file, int line, const char *reason
#define GRPC_COMBINER_REF(combiner, reason) \
  grpc_combiner_ref((combiner), __FILE__, __LINE__, (reason))
#define GRPC_COMBINER_UNREF(exec_ctx, combiner, reason) \
  grpc_combiner_unref((exec_ctx), (combiner), __FILE__, __LINE__, (reason))
#else
#define GRPC_COMBINER_DEBUG_ARGS
#define GRPC_COMBINER_REF(combiner, reason) grpc_combiner_ref((combiner))
#define GRPC_COMBINER_UNREF(exec_ctx, combiner, reason) \
  grpc_combiner_unref((exec_ctx), (combiner))
#endif

// Ref/unref the lock, for when we're sharing the lock ownership
// Prefer to use the macros above
grpc_combiner *grpc_combiner_ref(grpc_combiner *lock GRPC_COMBINER_DEBUG_ARGS);
void grpc_combiner_unref(grpc_exec_ctx *exec_ctx,
                         grpc_combiner *lock GRPC_COMBINER_DEBUG_ARGS);
// Fetch a scheduler to schedule closures against
grpc_closure_scheduler *grpc_combiner_scheduler(grpc_combiner *lock,
                                                bool covered_by_poller);
// Scheduler to execute \a action within the lock just prior to unlocking.
grpc_closure_scheduler *grpc_combiner_finally_scheduler(grpc_combiner *lock,
                                                        bool covered_by_poller);

bool grpc_combiner_continue_exec_ctx(grpc_exec_ctx *exec_ctx);

extern grpc_tracer_flag grpc_combiner_trace;

#endif /* GRPC_CORE_LIB_IOMGR_COMBINER_H */
