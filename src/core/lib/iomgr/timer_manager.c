/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/lib/iomgr/timer_manager.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/timer.h"

typedef struct completed_thread {
  gpr_thd_id t;
  struct completed_thread *next;
} completed_thread;

extern grpc_tracer_flag grpc_timer_check_trace;

// global mutex
static gpr_mu g_mu;
// are we multi-threaded
static bool g_threaded;
// cv to wait until a thread is needed
static gpr_cv g_cv_wait;
// cv for notification when threading ends
static gpr_cv g_cv_shutdown;
// number of threads in the system
static int g_thread_count;
// number of threads sitting around waiting
static int g_waiter_count;
// linked list of threads that have completed (and need joining)
static completed_thread *g_completed_threads;
// was the manager kicked by the timer system
static bool g_kicked;
// is there a thread waiting until the next timer should fire?
static bool g_has_timed_waiter;
// generation counter to track which thread is waiting for the next timer
static uint64_t g_timed_waiter_generation;

static void timer_thread(void *unused);

static void gc_completed_threads(void) {
  if (g_completed_threads != NULL) {
    completed_thread *to_gc = g_completed_threads;
    g_completed_threads = NULL;
    gpr_mu_unlock(&g_mu);
    while (to_gc != NULL) {
      gpr_thd_join(to_gc->t);
      completed_thread *next = to_gc->next;
      gpr_free(to_gc);
      to_gc = next;
    }
    gpr_mu_lock(&g_mu);
  }
}

static void start_timer_thread_and_unlock(void) {
  GPR_ASSERT(g_threaded);
  ++g_waiter_count;
  ++g_thread_count;
  gpr_mu_unlock(&g_mu);
  if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
    gpr_log(GPR_DEBUG, "Spawn timer thread");
  }
  gpr_thd_options opt = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&opt);
  completed_thread *ct = gpr_malloc(sizeof(*ct));
  gpr_thd_new(&ct->t, timer_thread, ct, &opt);
}

void grpc_timer_manager_tick() {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_timespec next = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
  grpc_timer_check(&exec_ctx, now, &next);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void timer_thread(void *completed_thread_ptr) {
  // this threads exec_ctx: we try to run things through to completion here
  // since it's easy to spin up new threads
  grpc_exec_ctx exec_ctx =
      GRPC_EXEC_CTX_INITIALIZER(0, grpc_never_ready_to_finish, NULL);
  const gpr_timespec inf_future = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  for (;;) {
    gpr_timespec next = inf_future;
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    // check timer state, updates next to the next time to run a check
    if (grpc_timer_check(&exec_ctx, now, &next)) {
      // if there's something to execute...
      gpr_mu_lock(&g_mu);
      // remove a waiter from the pool, and start another thread if necessary
      --g_waiter_count;
      if (g_waiter_count == 0 && g_threaded) {
        start_timer_thread_and_unlock();
      } else {
        // if there's no thread waiting with a timeout, kick an existing waiter
        // so that the next deadline is not missed
        if (!g_has_timed_waiter) {
          if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
            gpr_log(GPR_DEBUG, "kick untimed waiter");
          }
          gpr_cv_signal(&g_cv_wait);
        }
        gpr_mu_unlock(&g_mu);
      }
      // without our lock, flush the exec_ctx
      grpc_exec_ctx_flush(&exec_ctx);
      gpr_mu_lock(&g_mu);
      // garbage collect any threads hanging out that are dead
      gc_completed_threads();
      // get ready to wait again
      ++g_waiter_count;
      gpr_mu_unlock(&g_mu);
    } else {
      gpr_mu_lock(&g_mu);
      // if we're not threaded anymore, leave
      if (!g_threaded) break;
      // if there's no timed waiter, we should become one: that waiter waits
      // only until the next timer should expire
      // all other timers wait forever
      uint64_t my_timed_waiter_generation = g_timed_waiter_generation - 1;
      if (!g_has_timed_waiter) {
        g_has_timed_waiter = true;
        // we use a generation counter to track the timed waiter so we can
        // cancel an existing one quickly (and when it actually times out it'll
        // figure stuff out instead of incurring a wakeup)
        my_timed_waiter_generation = ++g_timed_waiter_generation;
        if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
          gpr_log(GPR_DEBUG, "sleep for a while");
        }
      } else {
        next = inf_future;
        if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
          gpr_log(GPR_DEBUG, "sleep until kicked");
        }
      }
      gpr_cv_wait(&g_cv_wait, &g_mu, next);
      if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
        gpr_log(GPR_DEBUG, "wait ended: was_timed:%d kicked:%d",
                my_timed_waiter_generation == g_timed_waiter_generation,
                g_kicked);
      }
      // if this was the timed waiter, then we need to check timers, and flag
      // that there's now no timed waiter... we'll look for a replacement if
      // there's work to do after checking timers (code above)
      if (my_timed_waiter_generation == g_timed_waiter_generation) {
        g_has_timed_waiter = false;
      }
      // if this was a kick from the timer system, consume it (and don't stop
      // this thread yet)
      if (g_kicked) {
        grpc_timer_consume_kick();
        g_kicked = false;
      }
      gpr_mu_unlock(&g_mu);
    }
  }
  // terminate the thread: drop the waiter count, thread count, and let whomever
  // stopped the threading stuff know that we're done
  --g_waiter_count;
  --g_thread_count;
  if (0 == g_thread_count) {
    gpr_cv_signal(&g_cv_shutdown);
  }
  completed_thread *ct = completed_thread_ptr;
  ct->next = g_completed_threads;
  g_completed_threads = ct;
  gpr_mu_unlock(&g_mu);
  grpc_exec_ctx_finish(&exec_ctx);
  if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
    gpr_log(GPR_DEBUG, "End timer thread");
  }
}

static void start_threads(void) {
  gpr_mu_lock(&g_mu);
  if (!g_threaded) {
    g_threaded = true;
    start_timer_thread_and_unlock();
  } else {
    g_threaded = false;
    gpr_mu_unlock(&g_mu);
  }
}

void grpc_timer_manager_init(void) {
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_cv_wait);
  gpr_cv_init(&g_cv_shutdown);
  g_threaded = false;
  g_thread_count = 0;
  g_waiter_count = 0;
  g_completed_threads = NULL;

  start_threads();
}

static void stop_threads(void) {
  gpr_mu_lock(&g_mu);
  if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
    gpr_log(GPR_DEBUG, "stop timer threads: threaded=%d", g_threaded);
  }
  if (g_threaded) {
    g_threaded = false;
    gpr_cv_broadcast(&g_cv_wait);
    if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
      gpr_log(GPR_DEBUG, "num timer threads: %d", g_thread_count);
    }
    while (g_thread_count > 0) {
      gpr_cv_wait(&g_cv_shutdown, &g_mu, gpr_inf_future(GPR_CLOCK_REALTIME));
      if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
        gpr_log(GPR_DEBUG, "num timer threads: %d", g_thread_count);
      }
      gc_completed_threads();
    }
  }
  gpr_mu_unlock(&g_mu);
}

void grpc_timer_manager_shutdown(void) {
  stop_threads();

  gpr_mu_destroy(&g_mu);
  gpr_cv_destroy(&g_cv_wait);
  gpr_cv_destroy(&g_cv_shutdown);
}

void grpc_timer_manager_set_threading(bool threaded) {
  if (threaded) {
    start_threads();
  } else {
    stop_threads();
  }
}

void grpc_kick_poller(void) {
  gpr_mu_lock(&g_mu);
  g_kicked = true;
  g_has_timed_waiter = false;
  ++g_timed_waiter_generation;
  gpr_cv_signal(&g_cv_wait);
  gpr_mu_unlock(&g_mu);
}
