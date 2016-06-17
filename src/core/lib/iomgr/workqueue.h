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

#ifndef GRPC_CORE_LIB_IOMGR_WORKQUEUE_H
#define GRPC_CORE_LIB_IOMGR_WORKQUEUE_H

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/pollset.h"

#ifdef GPR_POSIX_SOCKET
#include "src/core/lib/iomgr/workqueue_posix.h"
#endif

#ifdef GPR_WINDOWS
#include "src/core/lib/iomgr/workqueue_windows.h"
#endif

/* grpc_workqueue is forward declared in exec_ctx.h */

/** Create a work queue */
grpc_workqueue *grpc_workqueue_create(grpc_exec_ctx *exec_ctx);

void grpc_workqueue_flush(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue);

#define GRPC_WORKQUEUE_REFCOUNT_DEBUG
#ifdef GRPC_WORKQUEUE_REFCOUNT_DEBUG
#define GRPC_WORKQUEUE_REF(p, r) \
  grpc_workqueue_ref((p), __FILE__, __LINE__, (r))
#define GRPC_WORKQUEUE_UNREF(cl, p, r) \
  grpc_workqueue_unref((cl), (p), __FILE__, __LINE__, (r))
void grpc_workqueue_ref(grpc_workqueue *workqueue, const char *file, int line,
                        const char *reason);
void grpc_workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue,
                          const char *file, int line, const char *reason);
#else
#define GRPC_WORKQUEUE_REF(p, r) grpc_workqueue_ref((p))
#define GRPC_WORKQUEUE_UNREF(cl, p, r) grpc_workqueue_unref((cl), (p))
void grpc_workqueue_ref(grpc_workqueue *workqueue);
void grpc_workqueue_unref(grpc_exec_ctx *exec_ctx, grpc_workqueue *workqueue);
#endif

/** Bind this workqueue to a pollset */
void grpc_workqueue_add_to_pollset(grpc_exec_ctx *exec_ctx,
                                   grpc_workqueue *workqueue,
                                   grpc_pollset *pollset);

/** Add a work item to a workqueue */
void grpc_workqueue_push(grpc_workqueue *workqueue, grpc_closure *closure,
                         int success);

#endif /* GRPC_CORE_LIB_IOMGR_WORKQUEUE_H */
