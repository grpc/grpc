/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/iomgr/executor.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/tls.h>
#include <grpc/support/useful.h>

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/support/spinlock.h"

#define MAX_DEPTH 2

typedef struct {
  gpr_mu mu;
  gpr_cv cv;
  grpc_closure_list elems;
  size_t depth;
  bool shutdown;
  bool queued_long_job;
  gpr_thd_id id;
} thread_state;

static thread_state *g_thread_state;
/* Refcount to prevent g_thread_state from getting destroyed in cases where
   executor_push() races with grpc_executor_shutdown() */
static gpr_atm g_ts_refcount;

static size_t g_max_threads;
static gpr_atm g_cur_threads;

static gpr_spinlock g_adding_thread_lock = GPR_SPINLOCK_STATIC_INITIALIZER;

GPR_TLS_DECL(g_this_thread_state);

static grpc_tracer_flag executor_trace =
    GRPC_TRACER_INITIALIZER(false, "executor");

static void executor_thread(void *arg);

static void ts_destroy(grpc_exec_ctx *exec_ctx);

/* Adds a ref to the executor's thread state iff the current ref count == 0 */
static bool ts_init_ref() { return gpr_atm_full_cas(&g_ts_refcount, 0, 1); }

/* Adds a ref to the executor's thread state iff the current ref count is > 0 */
static bool ts_ref() {
  gpr_atm old_count;
  while (true) {
    old_count = gpr_atm_acq_load(&g_ts_refcount);
    if (old_count == 0) {
      return false;
    }

    if (gpr_atm_full_cas(&g_ts_refcount, old_count, old_count + 1)) {
      return true;
    }
  }
  GPR_UNREACHABLE_CODE(return false);
}

static void ts_unref(grpc_exec_ctx *exec_ctx) {
  if (gpr_atm_full_fetch_add(&g_ts_refcount, -1) == 1) {
    ts_destroy(exec_ctx);
    grpc_exec_ctx_flush(exec_ctx);
  }
}

static size_t run_closures(grpc_exec_ctx *exec_ctx, grpc_closure_list list) {
  size_t n = 0;

  grpc_closure *c = list.head;
  while (c != NULL) {
    grpc_closure *next = c->next_data.next;
    grpc_error *error = c->error_data.error;
    if (GRPC_TRACER_ON(executor_trace)) {
#ifndef NDEBUG
      gpr_log(GPR_DEBUG, "EXECUTOR: run %p [created by %s:%d]", c,
              c->file_created, c->line_created);
#else
      gpr_log(GPR_DEBUG, "EXECUTOR: run %p", c);
#endif
    }
#ifndef NDEBUG
    c->scheduled = false;
#endif
    c->cb(exec_ctx, c->cb_arg, error);
    GRPC_ERROR_UNREF(error);
    c = next;
    n++;
    grpc_exec_ctx_flush(exec_ctx);
  }

  return n;
}

bool grpc_executor_is_threaded() {
  return gpr_atm_no_barrier_load(&g_cur_threads) > 0;
}

void grpc_executor_set_threading(grpc_exec_ctx *exec_ctx, bool threading) {
  gpr_atm cur_threads = gpr_atm_no_barrier_load(&g_cur_threads);
  if (threading) {
    if (cur_threads > 0) return;

    GPR_ASSERT(ts_init_ref());  // Add the initial ref

    g_max_threads = GPR_MAX(1, 2 * gpr_cpu_num_cores());
    gpr_atm_no_barrier_store(&g_cur_threads, 1);
    gpr_tls_init(&g_this_thread_state);
    g_thread_state =
        (thread_state *)gpr_zalloc(sizeof(thread_state) * g_max_threads);
    for (size_t i = 0; i < g_max_threads; i++) {
      gpr_mu_init(&g_thread_state[i].mu);
      gpr_cv_init(&g_thread_state[i].cv);
      g_thread_state[i].elems = (grpc_closure_list)GRPC_CLOSURE_LIST_INIT;
    }

    gpr_thd_options opt = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&opt);
    gpr_thd_new(&g_thread_state[0].id, executor_thread, &g_thread_state[0],
                &opt);
  } else {
    if (cur_threads == 0) return;
    for (size_t i = 0; i < g_max_threads; i++) {
      gpr_mu_lock(&g_thread_state[i].mu);
      g_thread_state[i].shutdown = true;
      gpr_cv_signal(&g_thread_state[i].cv);
      gpr_mu_unlock(&g_thread_state[i].mu);
    }
    /* ensure no thread is adding a new thread... once this is past, then
       no thread will try to add a new one either (since shutdown is true) */
    gpr_spinlock_lock(&g_adding_thread_lock);
    gpr_spinlock_unlock(&g_adding_thread_lock);
    for (gpr_atm i = 0; i < gpr_atm_no_barrier_load(&g_cur_threads); i++) {
      gpr_thd_join(g_thread_state[i].id);
    }

    gpr_atm_no_barrier_store(&g_cur_threads, 0);
    gpr_tls_destroy(&g_this_thread_state);

    ts_unref(exec_ctx);  // Remove ref
  }
}

static void ts_destroy(grpc_exec_ctx *exec_ctx) {
  for (size_t i = 0; i < g_max_threads; i++) {
    gpr_mu_destroy(&g_thread_state[i].mu);
    gpr_cv_destroy(&g_thread_state[i].cv);
    run_closures(exec_ctx, g_thread_state[i].elems);
  }
  gpr_free(g_thread_state);
}

void grpc_executor_init(grpc_exec_ctx *exec_ctx) {
  grpc_register_tracer(&executor_trace);
  gpr_atm_no_barrier_store(&g_cur_threads, 0);
  gpr_atm_rel_store(&g_ts_refcount, 0);
  grpc_executor_set_threading(exec_ctx, true);
}

void grpc_executor_shutdown(grpc_exec_ctx *exec_ctx) {
  grpc_executor_set_threading(exec_ctx, false);
}

static void executor_thread(void *arg) {
  thread_state *ts = (thread_state *)arg;
  gpr_tls_set(&g_this_thread_state, (intptr_t)ts);

  grpc_exec_ctx exec_ctx =
      GRPC_EXEC_CTX_INITIALIZER(0, grpc_never_ready_to_finish, NULL);

  size_t subtract_depth = 0;
  for (;;) {
    if (GRPC_TRACER_ON(executor_trace)) {
      gpr_log(GPR_DEBUG, "EXECUTOR[%d]: step (sub_depth=%" PRIdPTR ")",
              (int)(ts - g_thread_state), subtract_depth);
    }
    gpr_mu_lock(&ts->mu);
    ts->depth -= subtract_depth;
    while (grpc_closure_list_empty(ts->elems) && !ts->shutdown) {
      ts->queued_long_job = false;
      gpr_cv_wait(&ts->cv, &ts->mu, gpr_inf_future(GPR_CLOCK_REALTIME));
    }
    if (ts->shutdown) {
      if (GRPC_TRACER_ON(executor_trace)) {
        gpr_log(GPR_DEBUG, "EXECUTOR[%d]: shutdown",
                (int)(ts - g_thread_state));
      }
      gpr_mu_unlock(&ts->mu);
      break;
    }
    GRPC_STATS_INC_EXECUTOR_QUEUE_DRAINED(&exec_ctx);
    grpc_closure_list exec = ts->elems;
    ts->elems = (grpc_closure_list)GRPC_CLOSURE_LIST_INIT;
    gpr_mu_unlock(&ts->mu);
    if (GRPC_TRACER_ON(executor_trace)) {
      gpr_log(GPR_DEBUG, "EXECUTOR[%d]: execute", (int)(ts - g_thread_state));
    }

    subtract_depth = run_closures(&exec_ctx, exec);
  }
  grpc_exec_ctx_finish(&exec_ctx);
}

static void executor_push(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                          grpc_error *error, bool is_short) {
  bool retry_push;
  if (is_short) {
    GRPC_STATS_INC_EXECUTOR_SCHEDULED_SHORT_ITEMS(exec_ctx);
  } else {
    GRPC_STATS_INC_EXECUTOR_SCHEDULED_LONG_ITEMS(exec_ctx);
  }

  /* Add a reference to g_thread_state to prevent it from getting destroyed */
  bool is_ref = ts_ref();

  do {
    retry_push = false;
    size_t cur_thread_count = (size_t)gpr_atm_no_barrier_load(&g_cur_threads);
    /* If current thread count is zero or if we do not have a reference to the
       thread state (i.e executor is shutdown and no longer available), just
       queue the closure on exec_ctx instead of the executor */
    if (cur_thread_count == 0 || !is_ref) {
      if (GRPC_TRACER_ON(executor_trace)) {
#ifndef NDEBUG
        gpr_log(GPR_DEBUG, "EXECUTOR: schedule %p (created %s:%d) inline",
                closure, closure->file_created, closure->line_created);
#else
        gpr_log(GPR_DEBUG, "EXECUTOR: schedule %p inline", closure);
#endif
      }
      grpc_closure_list_append(&exec_ctx->closure_list, closure, error);
      return;
    }
    thread_state *ts = (thread_state *)gpr_tls_get(&g_this_thread_state);
    if (ts == NULL) {
      ts = &g_thread_state[GPR_HASH_POINTER(exec_ctx, cur_thread_count)];
    } else {
      GRPC_STATS_INC_EXECUTOR_SCHEDULED_TO_SELF(exec_ctx);
    }
    thread_state *orig_ts = ts;

    bool try_new_thread;
    for (;;) {
      if (GRPC_TRACER_ON(executor_trace)) {
#ifndef NDEBUG
        gpr_log(
            GPR_DEBUG,
            "EXECUTOR: try to schedule %p (%s) (created %s:%d) to thread %d",
            closure, is_short ? "short" : "long", closure->file_created,
            closure->line_created, (int)(ts - g_thread_state));
#else
        gpr_log(GPR_DEBUG, "EXECUTOR: try to schedule %p (%s) to thread %d",
                closure, is_short ? "short" : "long",
                (int)(ts - g_thread_state));
#endif
      }

      gpr_mu_lock(&ts->mu);

      /* If thread state is shutdown, there is no point in continuing (since
         other thread states are also either shutdown or in the process of
         shutting down). */
      if (ts->shutdown) {
        gpr_mu_unlock(&ts->mu);
        ts_unref(exec_ctx);
        is_ref = false;
        continue;
      }

      if (ts->queued_long_job) {
        // if there's a long job queued, we never queue anything else to this
        // queue (since long jobs can take 'infinite' time and we need to
        // guarantee no starvation)
        // ... spin through queues and try again
        gpr_mu_unlock(&ts->mu);
        size_t idx = (size_t)(ts - g_thread_state);
        ts = &g_thread_state[(idx + 1) % cur_thread_count];
        if (ts == orig_ts) {
          retry_push = true;
          /* TODO (sreek). This should probably be try_new_thread =
           * !ts->shutdown */
          try_new_thread = true;
          break;
        }
        continue;
      }
      if (grpc_closure_list_empty(ts->elems)) {
        GRPC_STATS_INC_EXECUTOR_WAKEUP_INITIATED(exec_ctx);
        gpr_cv_signal(&ts->cv);
      }
      grpc_closure_list_append(&ts->elems, closure, error);
      ts->depth++;
      try_new_thread = ts->depth > MAX_DEPTH &&
                       cur_thread_count < g_max_threads && !ts->shutdown;
      if (!is_short) ts->queued_long_job = true;
      gpr_mu_unlock(&ts->mu);
      break;
    }

    if (try_new_thread && gpr_spinlock_trylock(&g_adding_thread_lock)) {
      cur_thread_count = (size_t)gpr_atm_no_barrier_load(&g_cur_threads);
      if (cur_thread_count < g_max_threads) {
        gpr_atm_no_barrier_store(&g_cur_threads, cur_thread_count + 1);

        gpr_thd_options opt = gpr_thd_options_default();
        gpr_thd_options_set_joinable(&opt);
        gpr_thd_new(&g_thread_state[cur_thread_count].id, executor_thread,
                    &g_thread_state[cur_thread_count], &opt);
      }
      gpr_spinlock_unlock(&g_adding_thread_lock);
    }

    if (retry_push) {
      GRPC_STATS_INC_EXECUTOR_PUSH_RETRIES(exec_ctx);
    }
  } while (retry_push);

  if (is_ref) {
    ts_unref(exec_ctx);
  }
}

static void executor_push_short(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                                grpc_error *error) {
  executor_push(exec_ctx, closure, error, true);
}

static void executor_push_long(grpc_exec_ctx *exec_ctx, grpc_closure *closure,
                               grpc_error *error) {
  executor_push(exec_ctx, closure, error, false);
}

static const grpc_closure_scheduler_vtable executor_vtable_short = {
    executor_push_short, executor_push_short, "executor"};
static grpc_closure_scheduler executor_scheduler_short = {
    &executor_vtable_short};

static const grpc_closure_scheduler_vtable executor_vtable_long = {
    executor_push_long, executor_push_long, "executor"};
static grpc_closure_scheduler executor_scheduler_long = {&executor_vtable_long};

grpc_closure_scheduler *grpc_executor_scheduler(
    grpc_executor_job_length length) {
  return length == GRPC_EXECUTOR_SHORT ? &executor_scheduler_short
                                       : &executor_scheduler_long;
}
