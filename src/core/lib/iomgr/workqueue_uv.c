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
