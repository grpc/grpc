/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_IOMGR_WORKQUEUE_H
#define GRPC_CORE_LIB_IOMGR_WORKQUEUE_H

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GPR_WINDOWS
#include "src/core/lib/iomgr/workqueue_windows.h"
#endif

/* grpc_workqueue is forward declared in exec_ctx.h */

/* Reference counting functions. Use the macro's always
   (GRPC_WORKQUEUE_{REF,UNREF}).

   Pass in a descriptive reason string for reffing/unreffing as the last
   argument to each macro. When GRPC_WORKQUEUE_REFCOUNT_DEBUG is defined, that
   string will be printed alongside the refcount. When it is not defined, the
   string will be discarded at compilation time. */

/*#define GRPC_WORKQUEUE_REFCOUNT_DEBUG*/
#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
#define GRPC_WORKQUEUE_REF(p, r) \
  grpc_workqueue_ref((p), __FILE__, __LINE__, (r))
#define GRPC_WORKQUEUE_UNREF(exec_ctx, p, r) \
  grpc_workqueue_unref((exec_ctx), (p), __FILE__, __LINE__, (r))
grpc_workqueue *grpc_workqueue_ref(grpc_workqueue *workqueue, const char *file,
                                   int line, const char *reason);
void grpc_workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue,
                          const char *file, int line, const char *reason);
#else
#define GRPC_WORKQUEUE_REF(p, r) grpc_workqueue_ref((p))
#define GRPC_WORKQUEUE_UNREF(cl, p, r) grpc_workqueue_unref((cl), (p))
grpc_workqueue *grpc_workqueue_ref(grpc_workqueue *workqueue);
void grpc_workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue);
#endif

/** Fetch the workqueue closure scheduler. Items added to a work queue will be
    started in approximately the order they were enqueued, on some thread that
    may or may not be the current thread. Successive closures enqueued onto a
    workqueue MAY be executed concurrently.

    It is generally more expensive to add a closure to a workqueue than to the
    execution context, both in terms of CPU work and in execution latency.

    Use work queues when it's important that other threads be given a chance to
    tackle some workload. */
grpc_closure_scheduler *grpc_workqueue_scheduler(grpc_workqueue *workqueue);

#endif /* GRPC_CORE_LIB_IOMGR_WORKQUEUE_H */
