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

#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_uv.h"

gpr_mu grpc_polling_mu;

size_t grpc_pollset_size() {
  return 1;
}

void grpc_pollset_global_init(void) {
  gpr_mu_init(&grpc_polling_mu);
}

void grpc_pollset_global_shutdown(void) { gpr_mu_destroy(&grpc_polling_mu); }

void grpc_pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  *mu = &grpc_polling_mu;
}

void grpc_pollset_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                             grpc_closure *closure) {
  grpc_exec_ctx_sched(exec_ctx, closure, GRPC_ERROR_NONE, NULL);
}

void grpc_pollset_destroy(grpc_pollset *pollset) {}

void grpc_pollset_reset(grpc_pollset *pollset) {}

grpc_error *grpc_pollset_work(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                              grpc_pollset_worker **worker_hdl,
                              gpr_timespec now, gpr_timespec deadline) {
  if (!grpc_closure_list_empty(exec_ctx->closure_list)) {
    gpr_mu_unlock(&grpc_polling_mu);
    grpc_exec_ctx_flush(exec_ctx);
    gpr_mu_lock(&grpc_polling_mu);
  }
  return GRPC_ERROR_NONE;
}

grpc_error *grpc_pollset_kick(grpc_pollset *pollset,
                              grpc_pollset_worker *specific_worker) {
  return GRPC_ERROR_NONE;
}

#endif /* GRPC_UV */
