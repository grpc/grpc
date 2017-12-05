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
  void (*body)(void* arg); /* body of a thread */
  void* arg;               /* argument to a thread */
};

static void inc_thd_count();
static void dec_thd_count();

/* Body of every thread started via gpr_thd_new. */
static void* thread_body(void* v) {
  struct thd_arg a = *(struct thd_arg*)v;
  free(v);
  (*a.body)(a.arg);
  dec_thd_count();
  return nullptr;
}

int gpr_thd_new(gpr_thd_id* t, void (*thd_body)(void* arg), void* arg,
                const gpr_thd_options* options) {
  int thread_started;
  pthread_attr_t attr;
  pthread_t p;
  /* don't use gpr_malloc as we may cause an infinite recursion with
   * the profiling code */
  struct thd_arg* a = (struct thd_arg*)malloc(sizeof(*a));
  GPR_ASSERT(a != nullptr);
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

void gpr_thd_join(gpr_thd_id t) { pthread_join((pthread_t)t, nullptr); }

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
