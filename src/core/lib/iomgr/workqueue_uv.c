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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_UV

#include "src/core/lib/iomgr/workqueue.h"

// Minimal implementation of grpc_workqueue for libuv
// Works by directly enqueuing workqueue items onto the current execution
// context, which is at least correct, if not performant or in the spirit of
// workqueues.

void grpc_workqueue_flush(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue) {}

#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
grpc_workqueue *grpc_workqueue_ref(grpc_workqueue *workqueue, const char *file,
                                   int line, const char *reason) {
  return workqueue;
}
void grpc_workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue,
                          const char *file, int line, const char *reason) {}
#else
grpc_workqueue *grpc_workqueue_ref(grpc_workqueue *workqueue) {
  return workqueue;
}
void grpc_workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue) {}
#endif

grpc_closure_scheduler *grpc_workqueue_scheduler(grpc_workqueue *workqueue) {
  return grpc_schedule_on_exec_ctx;
}

#endif /* GPR_UV */
