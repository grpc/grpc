/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_LIB_IOMGR_COMBINER_H
#define GRPC_CORE_LIB_IOMGR_COMBINER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <grpc/support/atm.h>
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/mpscq.h"
#include "src/core/lib/iomgr/exec_ctx.h"

// Provides serialized access to some resource.
// Each action queued on a combiner is executed serially in a borrowed thread.
// The actual thread executing actions may change over time (but there will only
// ever be one at a time).

// Initialize the lock, with an optional workqueue to shift load to when
// necessary
grpc_combiner* grpc_combiner_create(void);

#ifndef NDEBUG
#define GRPC_COMBINER_DEBUG_ARGS \
  , const char *file, int line, const char *reason
#define GRPC_COMBINER_REF(combiner, reason) \
  grpc_combiner_ref((combiner), __FILE__, __LINE__, (reason))
#define GRPC_COMBINER_UNREF(combiner, reason) \
  grpc_combiner_unref((combiner), __FILE__, __LINE__, (reason))
#else
#define GRPC_COMBINER_DEBUG_ARGS
#define GRPC_COMBINER_REF(combiner, reason) grpc_combiner_ref((combiner))
#define GRPC_COMBINER_UNREF(combiner, reason) grpc_combiner_unref((combiner))
#endif

// Ref/unref the lock, for when we're sharing the lock ownership
// Prefer to use the macros above
grpc_combiner* grpc_combiner_ref(grpc_combiner* lock GRPC_COMBINER_DEBUG_ARGS);
void grpc_combiner_unref(grpc_combiner* lock GRPC_COMBINER_DEBUG_ARGS);
// Fetch a scheduler to schedule closures against
grpc_closure_scheduler* grpc_combiner_scheduler(grpc_combiner* lock);
// Scheduler to execute \a action within the lock just prior to unlocking.
grpc_closure_scheduler* grpc_combiner_finally_scheduler(grpc_combiner* lock);

bool grpc_combiner_continue_exec_ctx();

extern grpc_core::DebugOnlyTraceFlag grpc_combiner_trace;

#endif /* GRPC_CORE_LIB_IOMGR_COMBINER_H */
