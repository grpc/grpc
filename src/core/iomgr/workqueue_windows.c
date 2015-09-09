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

#ifdef GPR_WIN32

#include <grpc/support/alloc.h>

#include "src/core/iomgr/workqueue.h"

struct grpc_workqueue {
  gpr_refcount refs;
};

grpc_workqueue *grpc_workqueue_create(void) {
  grpc_workqueue *workqueue = gpr_malloc(sizeof(grpc_workqueue));
  gpr_ref_init(&workqueue->refs, 1);
  return workqueue;
}

static void workqueue_destroy(grpc_workqueue *workqueue) {
  gpr_free(workqueue);
}

void grpc_workqueue_ref(grpc_workqueue *workqueue) {
  gpr_ref(&workqueue->refs);
}

void grpc_workqueue_unref(grpc_workqueue *workqueue) {
  if (gpr_unref(workqueue)) {
    workqueue_destroy(workqueue);
  }
}

void grpc_workqueue_add_to_pollset(grpc_workqueue *workqueue, grpc_pollset *pollset) {}

void grpc_workqueue_push(grpc_workqueue *workqueue, grpc_iomgr_closure *closure, int success) {
  /* TODO(ctiller): migrate current iomgr callback loop into this file */
  grpc_iomgr_add_delayed_callback(closure, success);
}

#endif /* GPR_WIN32 */
