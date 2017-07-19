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
// the deadline of the current timed waiter thread (only relevant if
// g_has_timed_waiter is true)
static gpr_timespec g_timed_waiter_deadline;
// generation counter to track which thread is waiting for the next timer
static uint64_t g_timed_waiter_generation;

static void timer_thread(void *completed_thread_ptr);

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
  // The call to gpr_thd_new() has to be under the same lock used by
  // gc_completed_threads(), particularly due to ct->t, which is written here
  // (internally by gpr_thd_new) and read there. Otherwise it's possible for ct
  // to leak through g_completed_threads and be freed in gc_completed_threads()
  // before "&ct->t" is written to, causing a use-after-free.
  gpr_mu_lock(&g_mu);
  gpr_thd_new(&ct->t, timer_thread, ct, &opt);
  gpr_mu_unlock(&g_mu);
}

void grpc_timer_manager_tick() {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_timespec next = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
  grpc_timer_check(&exec_ctx, now, &next);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void run_some_timers(grpc_exec_ctx *exec_ctx) {
  // if there's something to execute...
  gpr_mu_lock(&g_mu);
  // remove a waiter from the pool, and start another thread if necessary
  --g_waiter_count;
  if (g_waiter_count == 0 && g_threaded) {
    start_timer_thread_and_unlock();
  } else {
    // if there's no thread waiting with a timeout, kick an existing
    // waiter so that the next deadline is not missed
    if (!g_has_timed_waiter) {
      if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
        gpr_log(GPR_DEBUG, "kick untimed waiter");
      }
      gpr_cv_signal(&g_cv_wait);
    }
    gpr_mu_unlock(&g_mu);
  }
  // without our lock, flush the exec_ctx
  grpc_exec_ctx_flush(exec_ctx);
  gpr_mu_lock(&g_mu);
  // garbage collect any threads hanging out that are dead
  gc_completed_threads();
  // get ready to wait again
  ++g_waiter_count;
  gpr_mu_unlock(&g_mu);
}

// wait until 'next' (or forever if there is already a timed waiter in the pool)
// returns true if the thread should continue executing (false if it should
// shutdown)
static bool wait_until(gpr_timespec next) {
  const gpr_timespec inf_future = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  gpr_mu_lock(&g_mu);
  // if we're not threaded anymore, leave
  if (!g_threaded) {
    gpr_mu_unlock(&g_mu);
    return false;
  }

  // If g_kicked is true at this point, it means there was a kick from the timer
  // system that the timer-manager threads here missed. We cannot trust 'next'
  // here any longer (since there might be an earlier deadline). So if g_kicked
  // is true at this point, we should quickly exit this and get the next
  // deadline from the timer system

  if (!g_kicked) {
    // if there's no timed waiter, we should become one: that waiter waits
    // only until the next timer should expire. All other timers wait forever
    //
    // 'g_timed_waiter_generation' is a global generation counter. The idea here
    // is that the thread becoming a timed-waiter increments and stores this
    // global counter locally in 'my_timed_waiter_generation' before going to
    // sleep. After waking up, if my_timed_waiter_generation ==
    // g_timed_waiter_generation, it can be sure that it was the timed_waiter
    // thread (and that no other thread took over while this was asleep)
    //
    // Initialize my_timed_waiter_generation to some value that is NOT equal to
    // g_timed_waiter_generation
    uint64_t my_timed_waiter_generation = g_timed_waiter_generation - 1;

    /* If there's no timed waiter, we should become one: that waiter waits only
       until the next timer should expire. All other timer threads wait forever
       unless their 'next' is earlier than the current timed-waiter's deadline
       (in which case the thread with earlier 'next' takes over as the new timed
       waiter) */
    if (gpr_time_cmp(next, inf_future) != 0) {
      if (!g_has_timed_waiter ||
          (gpr_time_cmp(next, g_timed_waiter_deadline) < 0)) {
        my_timed_waiter_generation = ++g_timed_waiter_generation;
        g_has_timed_waiter = true;
        g_timed_waiter_deadline = next;

        if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
          gpr_timespec wait_time =
              gpr_time_sub(next, gpr_now(GPR_CLOCK_MONOTONIC));
          gpr_log(GPR_DEBUG, "sleep for a %" PRId64 ".%09d seconds",
                  wait_time.tv_sec, wait_time.tv_nsec);
        }
      } else {  // g_timed_waiter == true && next >= g_timed_waiter_deadline
        next = inf_future;
      }
    }

    if (GRPC_TRACER_ON(grpc_timer_check_trace) &&
        gpr_time_cmp(next, inf_future) == 0) {
      gpr_log(GPR_DEBUG, "sleep until kicked");
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
      g_timed_waiter_deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
    }
  }

  // if this was a kick from the timer system, consume it (and don't stop
  // this thread yet)
  if (g_kicked) {
    grpc_timer_consume_kick();
    g_kicked = false;
  }

  gpr_mu_unlock(&g_mu);
  return true;
}

static void timer_main_loop(grpc_exec_ctx *exec_ctx) {
  const gpr_timespec inf_future = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  for (;;) {
    gpr_timespec next = inf_future;
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    // check timer state, updates next to the next time to run a check
    switch (grpc_timer_check(exec_ctx, now, &next)) {
      case GRPC_TIMERS_FIRED:
        run_some_timers(exec_ctx);
        break;
      case GRPC_TIMERS_NOT_CHECKED:
        /* This case only happens under contention, meaning more than one timer
           manager thread checked timers concurrently.

           If that happens, we're guaranteed that some other thread has just
           checked timers, and this will avalanche into some other thread seeing
           empty timers and doing a timed sleep.

           Consequently, we can just sleep forever here and be happy at some
           saved wakeup cycles. */
        if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
          gpr_log(GPR_DEBUG, "timers not checked: expect another thread to");
        }
        next = inf_future;
      /* fall through */
      case GRPC_TIMERS_CHECKED_AND_EMPTY:
        if (!wait_until(next)) {
          return;
        }
        break;
    }
  }
}

static void timer_thread_cleanup(completed_thread *ct) {
  gpr_mu_lock(&g_mu);
  // terminate the thread: drop the waiter count, thread count, and let whomever
  // stopped the threading stuff know that we're done
  --g_waiter_count;
  --g_thread_count;
  if (0 == g_thread_count) {
    gpr_cv_signal(&g_cv_shutdown);
  }
  ct->next = g_completed_threads;
  g_completed_threads = ct;
  gpr_mu_unlock(&g_mu);
  if (GRPC_TRACER_ON(grpc_timer_check_trace)) {
    gpr_log(GPR_DEBUG, "End timer thread");
  }
}

static void timer_thread(void *completed_thread_ptr) {
  // this threads exec_ctx: we try to run things through to completion here
  // since it's easy to spin up new threads
  grpc_exec_ctx exec_ctx =
      GRPC_EXEC_CTX_INITIALIZER(0, grpc_never_ready_to_finish, NULL);
  timer_main_loop(&exec_ctx);
  grpc_exec_ctx_finish(&exec_ctx);
  timer_thread_cleanup(completed_thread_ptr);
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

  g_has_timed_waiter = false;
  g_timed_waiter_deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);

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
  g_timed_waiter_deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  ++g_timed_waiter_generation;
  gpr_cv_signal(&g_cv_wait);
  gpr_mu_unlock(&g_mu);
}
