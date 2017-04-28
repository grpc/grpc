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

#if GRPC_UV

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/timer.h"

#include <uv.h>

static void timer_close_callback(uv_handle_t *handle) { gpr_free(handle); }

static void stop_uv_timer(uv_timer_t *handle) {
  uv_timer_stop(handle);
  uv_unref((uv_handle_t *)handle);
  uv_close((uv_handle_t *)handle, timer_close_callback);
}

void run_expired_timer(uv_timer_t *handle) {
  grpc_timer *timer = (grpc_timer *)handle->data;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GPR_ASSERT(timer->pending);
  timer->pending = 0;
  grpc_closure_sched(&exec_ctx, timer->closure, GRPC_ERROR_NONE);
  stop_uv_timer(handle);
  grpc_exec_ctx_finish(&exec_ctx);
}

void grpc_timer_init(grpc_exec_ctx *exec_ctx, grpc_timer *timer,
                     gpr_timespec deadline, grpc_closure *closure,
                     gpr_timespec now) {
  uint64_t timeout;
  uv_timer_t *uv_timer;
  timer->closure = closure;
  if (gpr_time_cmp(deadline, now) <= 0) {
    timer->pending = 0;
    grpc_closure_sched(exec_ctx, timer->closure, GRPC_ERROR_NONE);
    return;
  }
  timer->pending = 1;
  timeout = (uint64_t)gpr_time_to_millis(gpr_time_sub(deadline, now));
  uv_timer = gpr_malloc(sizeof(uv_timer_t));
  uv_timer_init(uv_default_loop(), uv_timer);
  uv_timer->data = timer;
  timer->uv_timer = uv_timer;
  uv_timer_start(uv_timer, run_expired_timer, timeout, 0);
  /* We assume that gRPC timers are only used alongside other active gRPC
     objects, and that there will therefore always be something else keeping
     the uv loop alive whenever there is a timer */
  uv_unref((uv_handle_t *)uv_timer);
}

void grpc_timer_cancel(grpc_exec_ctx *exec_ctx, grpc_timer *timer) {
  if (timer->pending) {
    timer->pending = 0;
    grpc_closure_sched(exec_ctx, timer->closure, GRPC_ERROR_CANCELLED);
    stop_uv_timer((uv_timer_t *)timer->uv_timer);
  }
}

bool grpc_timer_check(grpc_exec_ctx *exec_ctx, gpr_timespec now,
                      gpr_timespec *next) {
  return false;
}

void grpc_timer_list_init(gpr_timespec now) {}
void grpc_timer_list_shutdown(grpc_exec_ctx *exec_ctx) {}

#endif /* GRPC_UV */
