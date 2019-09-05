/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/iomgr_custom.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/timer_custom.h"

static grpc_custom_timer_vtable* custom_timer_impl;

void grpc_custom_timer_callback(grpc_custom_timer* t, grpc_error* error) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_timer* timer = t->original;
  GPR_ASSERT(timer->pending);
  timer->pending = 0;
  GRPC_CLOSURE_SCHED(timer->closure, GRPC_ERROR_NONE);
  custom_timer_impl->stop(t);
  gpr_free(t);
}

static void timer_init(grpc_timer* timer, grpc_millis deadline,
                       grpc_closure* closure) {
  uint64_t timeout;
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  grpc_millis now = grpc_core::ExecCtx::Get()->Now();
  if (deadline <= grpc_core::ExecCtx::Get()->Now()) {
    GRPC_CLOSURE_SCHED(closure, GRPC_ERROR_NONE);
    timer->pending = false;
    return;
  } else {
    timeout = deadline - now;
  }
  timer->pending = true;
  timer->closure = closure;
  grpc_custom_timer* timer_wrapper =
      (grpc_custom_timer*)gpr_malloc(sizeof(grpc_custom_timer));
  timer_wrapper->timeout_ms = timeout;
  timer->custom_timer = (void*)timer_wrapper;
  timer_wrapper->original = timer;
  custom_timer_impl->start(timer_wrapper);
}

static void timer_cancel(grpc_timer* timer) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  grpc_custom_timer* tw = (grpc_custom_timer*)timer->custom_timer;
  if (timer->pending) {
    timer->pending = 0;
    GRPC_CLOSURE_SCHED(timer->closure, GRPC_ERROR_CANCELLED);
    custom_timer_impl->stop(tw);
    gpr_free(tw);
  }
}

static grpc_timer_check_result timer_check(grpc_millis* next) {
  return GRPC_TIMERS_NOT_CHECKED;
}

static void timer_list_init() {}
static void timer_list_shutdown() {}

static void timer_consume_kick(void) {}

static grpc_timer_vtable custom_timer_vtable = {
    timer_init,      timer_cancel,        timer_check,
    timer_list_init, timer_list_shutdown, timer_consume_kick};

void grpc_custom_timer_init(grpc_custom_timer_vtable* impl) {
  custom_timer_impl = impl;
  grpc_set_timer_impl(&custom_timer_vtable);
}
