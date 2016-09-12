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

#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include "src/core/lib/iomgr/workqueue.h"

// Minimal implementation of grpc_workqueue for Windows
// Works by directly enqueuing workqueue items onto the current execution
// context, which is at least correct, if not performant or in the spirit of
// workqueues.

void grpc_workqueue_flush(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue) {}

#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
void grpc_workqueue_ref(grpc_workqueue *workqueue, const char *file, int line,
                        const char *reason) {}
void grpc_workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue,
                          const char *file, int line, const char *reason) {}
#else
void grpc_workqueue_ref(grpc_workqueue *workqueue) {}
void grpc_workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue) {}
#endif

void grpc_workqueue_enqueue(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue,
                            grpc_closure *closure, grpc_error *error) {
  grpc_exec_ctx_sched(exec_ctx, closure, error, NULL);
}

#endif /* GPR_WINDOWS */
