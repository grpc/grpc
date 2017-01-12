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

#include <uv.h>

#include <string.h>

#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_uv.h"

struct grpc_pollset {
  uv_timer_t timer;
  int shutting_down;
};

/* Indicates that grpc_pollset_work should run an iteration of the UV loop
   before running callbacks. This defaults to 1, and should be disabled if
   grpc_pollset_work will be called within the callstack of uv_run */
int grpc_pollset_work_run_loop;

gpr_mu grpc_polling_mu;

size_t grpc_pollset_size() { return sizeof(grpc_pollset); }

void grpc_pollset_global_init(void) {
  gpr_mu_init(&grpc_polling_mu);
  grpc_pollset_work_run_loop = 1;
}

void grpc_pollset_global_shutdown(void) { gpr_mu_destroy(&grpc_polling_mu); }

void grpc_pollset_init(grpc_pollset *pollset, gpr_mu **mu) {
  *mu = &grpc_polling_mu;
  memset(pollset, 0, sizeof(grpc_pollset));
  uv_timer_init(uv_default_loop(), &pollset->timer);
  pollset->shutting_down = 0;
}

static void timer_close_cb(uv_handle_t *handle) { handle->data = (void *)1; }

void grpc_pollset_shutdown(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                           grpc_closure *closure) {
  GPR_ASSERT(!pollset->shutting_down);
  pollset->shutting_down = 1;
  if (grpc_pollset_work_run_loop) {
    // Drain any pending UV callbacks without blocking
    uv_run(uv_default_loop(), UV_RUN_NOWAIT);
  }
  grpc_closure_sched(exec_ctx, closure, GRPC_ERROR_NONE);
}

void grpc_pollset_destroy(grpc_pollset *pollset) {
  uv_close((uv_handle_t *)&pollset->timer, timer_close_cb);
  // timer.data is a boolean indicating that the timer has finished closing
  pollset->timer.data = (void *)0;
  if (grpc_pollset_work_run_loop) {
    while (!pollset->timer.data) {
      uv_run(uv_default_loop(), UV_RUN_NOWAIT);
    }
  }
}

void grpc_pollset_reset(grpc_pollset *pollset) {
  GPR_ASSERT(pollset->shutting_down);
  pollset->shutting_down = 0;
}

static void timer_run_cb(uv_timer_t *timer) {}

grpc_error *grpc_pollset_work(grpc_exec_ctx *exec_ctx, grpc_pollset *pollset,
                              grpc_pollset_worker **worker_hdl,
                              gpr_timespec now, gpr_timespec deadline) {
  uint64_t timeout;
  gpr_mu_unlock(&grpc_polling_mu);
  if (grpc_pollset_work_run_loop) {
    if (gpr_time_cmp(deadline, now) >= 0) {
      timeout = (uint64_t)gpr_time_to_millis(gpr_time_sub(deadline, now));
    } else {
      timeout = 0;
    }
    /* We special-case timeout=0 so that we don't bother with the timer when
       the loop won't block anyway */
    if (timeout > 0) {
      uv_timer_start(&pollset->timer, timer_run_cb, timeout, 0);
      /* Run until there is some I/O activity or the timer triggers. It doesn't
         matter which happens */
      uv_run(uv_default_loop(), UV_RUN_ONCE);
      uv_timer_stop(&pollset->timer);
    } else {
      uv_run(uv_default_loop(), UV_RUN_NOWAIT);
    }
  }
  if (!grpc_closure_list_empty(exec_ctx->closure_list)) {
    grpc_exec_ctx_flush(exec_ctx);
  }
  gpr_mu_lock(&grpc_polling_mu);
  return GRPC_ERROR_NONE;
}

grpc_error *grpc_pollset_kick(grpc_pollset *pollset,
                              grpc_pollset_worker *specific_worker) {
  return GRPC_ERROR_NONE;
}

#endif /* GRPC_UV */
