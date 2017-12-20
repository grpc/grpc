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

#ifdef GRPC_UV

#include <uv.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/iomgr_uv.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_uv.h"

#include "src/core/lib/debug/trace.h"

grpc_core::DebugOnlyTraceFlag grpc_trace_fd_refcount(false, "fd_refcount");

struct grpc_pollset {
  uv_timer_t* timer;
  int shutting_down;
};

/* Indicates that grpc_pollset_work should run an iteration of the UV loop
   before running callbacks. This defaults to 1, and should be disabled if
   grpc_pollset_work will be called within the callstack of uv_run */
int grpc_pollset_work_run_loop;

gpr_mu grpc_polling_mu;

/* This is used solely to kick the uv loop, by setting a callback to be run
   immediately in the next loop iteration.
   Note: In the future, if there is a bug that involves missing wakeups in the
   future, try adding a uv_async_t to kick the loop differently */
uv_timer_t* dummy_uv_handle;

size_t grpc_pollset_size() { return sizeof(grpc_pollset); }

void dummy_timer_cb(uv_timer_t* handle) {}

void dummy_handle_close_cb(uv_handle_t* handle) { gpr_free(handle); }

void grpc_pollset_global_init(void) {
  gpr_mu_init(&grpc_polling_mu);
  dummy_uv_handle = (uv_timer_t*)gpr_malloc(sizeof(uv_timer_t));
  uv_timer_init(uv_default_loop(), dummy_uv_handle);
  grpc_pollset_work_run_loop = 1;
}

void grpc_pollset_global_shutdown(void) {
  GRPC_UV_ASSERT_SAME_THREAD();
  gpr_mu_destroy(&grpc_polling_mu);
  uv_close((uv_handle_t*)dummy_uv_handle, dummy_handle_close_cb);
}

static void timer_run_cb(uv_timer_t* timer) {}

static void timer_close_cb(uv_handle_t* handle) {
  handle->data = (void*)1;
  gpr_free(handle);
}

void grpc_pollset_init(grpc_pollset* pollset, gpr_mu** mu) {
  GRPC_UV_ASSERT_SAME_THREAD();
  *mu = &grpc_polling_mu;
  pollset->timer = (uv_timer_t*)gpr_malloc(sizeof(uv_timer_t));
  uv_timer_init(uv_default_loop(), pollset->timer);
  pollset->shutting_down = 0;
}

void grpc_pollset_shutdown(grpc_pollset* pollset, grpc_closure* closure) {
  GPR_ASSERT(!pollset->shutting_down);
  GRPC_UV_ASSERT_SAME_THREAD();
  pollset->shutting_down = 1;
  if (grpc_pollset_work_run_loop) {
    // Drain any pending UV callbacks without blocking
    uv_run(uv_default_loop(), UV_RUN_NOWAIT);
  } else {
    // kick the loop once
    uv_timer_start(dummy_uv_handle, dummy_timer_cb, 0, 0);
  }
  GRPC_CLOSURE_SCHED(closure, GRPC_ERROR_NONE);
}

void grpc_pollset_destroy(grpc_pollset* pollset) {
  GRPC_UV_ASSERT_SAME_THREAD();
  uv_close((uv_handle_t*)pollset->timer, timer_close_cb);
  // timer.data is a boolean indicating that the timer has finished closing
  pollset->timer->data = (void*)0;
  if (grpc_pollset_work_run_loop) {
    while (!pollset->timer->data) {
      uv_run(uv_default_loop(), UV_RUN_NOWAIT);
    }
  }
}

grpc_error* grpc_pollset_work(grpc_pollset* pollset,
                              grpc_pollset_worker** worker_hdl,
                              grpc_millis deadline) {
  uint64_t timeout;
  GRPC_UV_ASSERT_SAME_THREAD();
  gpr_mu_unlock(&grpc_polling_mu);
  if (grpc_pollset_work_run_loop) {
    grpc_millis now = grpc_core::ExecCtx::Get()->Now();
    if (deadline >= now) {
      timeout = deadline - now;
    } else {
      timeout = 0;
    }
    /* We special-case timeout=0 so that we don't bother with the timer when
       the loop won't block anyway */
    if (timeout > 0) {
      uv_timer_start(pollset->timer, timer_run_cb, timeout, 0);
      /* Run until there is some I/O activity or the timer triggers. It doesn't
         matter which happens */
      uv_run(uv_default_loop(), UV_RUN_ONCE);
      uv_timer_stop(pollset->timer);
    } else {
      uv_run(uv_default_loop(), UV_RUN_NOWAIT);
    }
  }
  if (!grpc_closure_list_empty(*grpc_core::ExecCtx::Get()->closure_list())) {
    grpc_core::ExecCtx::Get()->Flush();
  }
  gpr_mu_lock(&grpc_polling_mu);
  return GRPC_ERROR_NONE;
}

grpc_error* grpc_pollset_kick(grpc_pollset* pollset,
                              grpc_pollset_worker* specific_worker) {
  GRPC_UV_ASSERT_SAME_THREAD();
  uv_timer_start(dummy_uv_handle, dummy_timer_cb, 0, 0);
  return GRPC_ERROR_NONE;
}

#endif /* GRPC_UV */
