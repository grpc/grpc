/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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
