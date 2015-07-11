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

/* Test of gpr_cancellable. */

#include <stdio.h>
#include <stdlib.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include "test/core/util/test_config.h"

struct test {
  gpr_mu mu;
  gpr_cv cv;
  gpr_event ev;
  gpr_event done;
  gpr_cancellable cancel;
  int n;
};

/* A thread body.   Wait until t->cancel is cancelledm then
   decrement t->n.  If t->n becomes 0, set t->done.  */
static void thd_body(void *v) {
  struct test *t = v;
  gpr_mu_lock(&t->mu);
  while (!gpr_cv_cancellable_wait(&t->cv, &t->mu, gpr_inf_future, &t->cancel)) {
  }
  t->n--;
  if (t->n == 0) {
    gpr_event_set(&t->done, (void *)1);
  }
  gpr_mu_unlock(&t->mu);
}

static void test(void) {
  int i;
  gpr_thd_id thd;
  struct test t;
  int n = 1;
  gpr_timespec interval;

  gpr_mu_init(&t.mu);
  gpr_cv_init(&t.cv);
  gpr_event_init(&t.ev);
  gpr_event_init(&t.done);
  gpr_cancellable_init(&t.cancel);

  /* A gpr_cancellable starts not cancelled. */
  GPR_ASSERT(!gpr_cancellable_is_cancelled(&t.cancel));

  /* Test timeout on event wait for uncancelled gpr_cancellable */
  interval = gpr_now(GPR_CLOCK_REALTIME);
  gpr_event_cancellable_wait(&t.ev, gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                                 gpr_time_from_micros(1000000)),
                             &t.cancel);
  interval = gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), interval);
  GPR_ASSERT(gpr_time_cmp(interval, gpr_time_from_micros(500000)) >= 0);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_micros(2000000), interval) >= 0);

  /* Test timeout on cv wait for uncancelled gpr_cancellable */
  gpr_mu_lock(&t.mu);
  interval = gpr_now(GPR_CLOCK_REALTIME);
  while (!gpr_cv_cancellable_wait(
      &t.cv, &t.mu,
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_micros(1000000)),
      &t.cancel)) {
  }
  interval = gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), interval);
  GPR_ASSERT(gpr_time_cmp(interval, gpr_time_from_micros(500000)) >= 0);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_micros(2000000), interval) >= 0);
  gpr_mu_unlock(&t.mu);

  /* Create some threads.  They all wait until cancelled; the last to finish
     sets t.done.  */
  t.n = n;
  for (i = 0; i != n; i++) {
    GPR_ASSERT(gpr_thd_new(&thd, &thd_body, &t, NULL));
  }
  /* Check that t.cancel still is not cancelled. */
  GPR_ASSERT(!gpr_cancellable_is_cancelled(&t.cancel));

  /* Wait a second, and check that no threads have finished waiting. */
  gpr_mu_lock(&t.mu);
  gpr_cv_wait(&t.cv, &t.mu, gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                         gpr_time_from_micros(1000000)));
  GPR_ASSERT(t.n == n);
  gpr_mu_unlock(&t.mu);

  /* Check that t.cancel still is not cancelled, but when
     cancelled it retports that it is cacncelled. */
  GPR_ASSERT(!gpr_cancellable_is_cancelled(&t.cancel));
  gpr_cancellable_cancel(&t.cancel);
  GPR_ASSERT(gpr_cancellable_is_cancelled(&t.cancel));

  /* Wait for threads to finish. */
  gpr_event_wait(&t.done, gpr_inf_future);
  GPR_ASSERT(t.n == 0);

  /* Test timeout on cv wait for cancelled gpr_cancellable */
  gpr_mu_lock(&t.mu);
  interval = gpr_now(GPR_CLOCK_REALTIME);
  while (!gpr_cv_cancellable_wait(
      &t.cv, &t.mu,
      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_micros(1000000)),
      &t.cancel)) {
  }
  interval = gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), interval);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_micros(100000), interval) >= 0);
  gpr_mu_unlock(&t.mu);

  /* Test timeout on event wait for cancelled gpr_cancellable */
  interval = gpr_now(GPR_CLOCK_REALTIME);
  gpr_event_cancellable_wait(&t.ev, gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                                 gpr_time_from_micros(1000000)),
                             &t.cancel);
  interval = gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), interval);
  GPR_ASSERT(gpr_time_cmp(gpr_time_from_micros(100000), interval) >= 0);

  gpr_mu_destroy(&t.mu);
  gpr_cv_destroy(&t.cv);
  gpr_cancellable_destroy(&t.cancel);
}

/* ------------------------------------------------- */

int main(int argc, char *argv[]) {
  grpc_test_init(argc, argv);
  test();
  return 0;
}
