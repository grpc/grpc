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

#include "src/core/lib/iomgr/timer.h"

typedef struct completed_thread {
  gpr_thd_id t;
  struct completed_thread *next;
} completed_thread;

static gpr_mu g_mu;
static gpr_cv g_cv_wait;
static gpr_cv g_cv_shutdown;
static int g_thread_count;
static int g_waiter_count;
static bool g_shutdown;
static completed_thread *g_completed_threads;
static bool g_kicked;

#define MAX_WAITERS 3

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
  ++g_waiter_count;
  ++g_thread_count;
  gpr_mu_unlock(&g_mu);
  gpr_log(GPR_DEBUG, "Spawn timer thread");
  gpr_thd_id thd;
  gpr_thd_options opt = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&opt);
  gpr_thd_new(&thd, timer_thread, NULL, &opt);
}

static void timer_thread(void *unused) {
  grpc_exec_ctx exec_ctx =
      GRPC_EXEC_CTX_INITIALIZER(0, grpc_never_ready_to_finish, NULL);
  for (;;) {
    gpr_timespec next = gpr_inf_future(GPR_CLOCK_MONOTONIC);
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    if (grpc_timer_check(&exec_ctx, now, &next)) {
      gpr_mu_lock(&g_mu);
      --g_waiter_count;
      bool start_thread = g_waiter_count == 0;
      if (start_thread && !g_shutdown) {
        start_timer_thread_and_unlock();
      } else {
        gpr_mu_unlock(&g_mu);
      }
      grpc_exec_ctx_flush(&exec_ctx);
      gpr_mu_lock(&g_mu);
      gc_completed_threads();
      ++g_waiter_count;
      gpr_mu_unlock(&g_mu);
    } else {
      gpr_mu_lock(&g_mu);
      if (g_shutdown) break;
      if (gpr_cv_wait(&g_cv_wait, &g_mu, next)) {
        if (g_kicked) {
          grpc_timer_consume_kick();
          g_kicked = false;
        } else if (g_waiter_count > MAX_WAITERS) {
          break;
        }
      }
      gpr_mu_unlock(&g_mu);
    }
  }
  --g_waiter_count;
  --g_thread_count;
  if (0 == g_thread_count) {
    gpr_cv_signal(&g_cv_shutdown);
  }
  completed_thread *ct = gpr_malloc(sizeof(*ct));
  ct->t = gpr_thd_currentid();
  ct->next = g_completed_threads;
  g_completed_threads = ct;
  gpr_mu_unlock(&g_mu);
  gpr_log(GPR_DEBUG, "End timer thread");
}

void grpc_timer_manager_init(void) {
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_cv_wait);
  gpr_cv_init(&g_cv_shutdown);
  g_thread_count = 0;
  g_waiter_count = 0;
  g_shutdown = false;
  g_completed_threads = NULL;

  gpr_mu_lock(&g_mu);
  start_timer_thread_and_unlock();
}

void grpc_timer_manager_shutdown(void) {
  gpr_mu_lock(&g_mu);
  g_shutdown = true;
  gpr_cv_broadcast(&g_cv_wait);
  while (g_thread_count > 0) {
    gpr_cv_wait(&g_cv_shutdown, &g_mu, gpr_inf_future(GPR_CLOCK_REALTIME));
    gc_completed_threads();
  }
  gpr_mu_unlock(&g_mu);

  gpr_mu_destroy(&g_mu);
  gpr_cv_destroy(&g_cv_wait);
  gpr_cv_destroy(&g_cv_shutdown);
}

void grpc_kick_poller(void) {
  gpr_mu_lock(&g_mu);
  g_kicked = true;
  gpr_cv_signal(&g_cv_wait);
  gpr_mu_unlock(&g_mu);
}
