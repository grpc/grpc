/*
 *
 * Copyright 2015, Google Inc.
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

/* Posix implementation for gpr threads. */

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SYNC

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/lib/support/fork.h"

static gpr_mu g_mu;
static gpr_cv g_cv;
static int g_thread_count;
static int g_awaiting_threads;

struct thd_arg {
  void (*body)(void *arg); /* body of a thread */
  void *arg;               /* argument to a thread */
};

static void inc_thd_count();
static void dec_thd_count();

/* Body of every thread started via gpr_thd_new. */
static void *thread_body(void *v) {
  struct thd_arg a = *(struct thd_arg *)v;
  free(v);
  (*a.body)(a.arg);
  dec_thd_count();
  return NULL;
}

int gpr_thd_new(gpr_thd_id *t, void (*thd_body)(void *arg), void *arg,
                const gpr_thd_options *options) {
  int thread_started;
  pthread_attr_t attr;
  pthread_t p;
  /* don't use gpr_malloc as we may cause an infinite recursion with
   * the profiling code */
  struct thd_arg *a = malloc(sizeof(*a));
  GPR_ASSERT(a != NULL);
  a->body = thd_body;
  a->arg = arg;
  inc_thd_count();

  GPR_ASSERT(pthread_attr_init(&attr) == 0);
  if (gpr_thd_options_is_detached(options)) {
    GPR_ASSERT(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) ==
               0);
  } else {
    GPR_ASSERT(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) ==
               0);
  }
  thread_started = (pthread_create(&p, &attr, &thread_body, a) == 0);
  GPR_ASSERT(pthread_attr_destroy(&attr) == 0);
  if (!thread_started) {
    /* don't use gpr_free, as this was allocated using malloc (see above) */
    free(a);
    dec_thd_count();
  }
  *t = (gpr_thd_id)p;
  return thread_started;
}

gpr_thd_id gpr_thd_currentid(void) { return (gpr_thd_id)pthread_self(); }

void gpr_thd_join(gpr_thd_id t) { pthread_join((pthread_t)t, NULL); }

/*****************************************
 * Only used when fork support is enabled
 */

static void inc_thd_count() {
  if (grpc_fork_support_enabled()) {
    gpr_mu_lock(&g_mu);
    g_thread_count++;
    gpr_mu_unlock(&g_mu);
  }
}

static void dec_thd_count() {
  if (grpc_fork_support_enabled()) {
    gpr_mu_lock(&g_mu);
    g_thread_count--;
    if (g_awaiting_threads && g_thread_count == 0) {
      gpr_cv_signal(&g_cv);
    }
    gpr_mu_unlock(&g_mu);
  }
}

void gpr_thd_init() {
  gpr_mu_init(&g_mu);
  gpr_cv_init(&g_cv);
  g_thread_count = 0;
  g_awaiting_threads = 0;
}

int gpr_await_threads(gpr_timespec deadline) {
  gpr_mu_lock(&g_mu);
  g_awaiting_threads = 1;
  int res = 0;
  if (g_thread_count > 0) {
    res = gpr_cv_wait(&g_cv, &g_mu, deadline);
  }
  g_awaiting_threads = 0;
  gpr_mu_unlock(&g_mu);
  return res == 0;
}

#endif /* GPR_POSIX_SYNC */
