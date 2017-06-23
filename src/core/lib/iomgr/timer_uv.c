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

#if GRPC_UV

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/timer.h"

#include <uv.h>

grpc_tracer_flag grpc_timer_trace = GRPC_TRACER_INITIALIZER(false);
grpc_tracer_flag grpc_timer_check_trace = GRPC_TRACER_INITIALIZER(false);

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
  GRPC_CLOSURE_SCHED(&exec_ctx, timer->closure, GRPC_ERROR_NONE);
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
    GRPC_CLOSURE_SCHED(exec_ctx, timer->closure, GRPC_ERROR_NONE);
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
    GRPC_CLOSURE_SCHED(exec_ctx, timer->closure, GRPC_ERROR_CANCELLED);
    stop_uv_timer((uv_timer_t *)timer->uv_timer);
  }
}

grpc_timer_check_result grpc_timer_check(grpc_exec_ctx *exec_ctx,
                                         gpr_timespec now, gpr_timespec *next) {
  return GRPC_TIMERS_NOT_CHECKED;
}

void grpc_timer_list_init(gpr_timespec now) {}
void grpc_timer_list_shutdown(grpc_exec_ctx *exec_ctx) {}

void grpc_timer_consume_kick(void) {}

#endif /* GRPC_UV */
