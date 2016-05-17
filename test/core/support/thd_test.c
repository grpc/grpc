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

/* Test of gpr thread support. */

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <stdio.h>
#include <stdlib.h>
#include "test/core/util/test_config.h"

#define NUM_THREADS 300

struct test {
  gpr_mu mu;
  int n;
  int is_done;
  gpr_cv done_cv;
};

/* A Thread body.   Decrement t->n, and if is becomes zero, set t->done. */
static void thd_body(void *v) {
  struct test *t = v;
  gpr_mu_lock(&t->mu);
  t->n--;
  if (t->n == 0) {
    t->is_done = 1;
    gpr_cv_signal(&t->done_cv);
  }
  gpr_mu_unlock(&t->mu);
}

static void thd_body_joinable(void *v) {}

/* Test thread options work as expected */
static void test_options(void) {
  gpr_thd_options options = gpr_thd_options_default();
  GPR_ASSERT(!gpr_thd_options_is_joinable(&options));
  GPR_ASSERT(gpr_thd_options_is_detached(&options));
  gpr_thd_options_set_joinable(&options);
  GPR_ASSERT(gpr_thd_options_is_joinable(&options));
  GPR_ASSERT(!gpr_thd_options_is_detached(&options));
  gpr_thd_options_set_detached(&options);
  GPR_ASSERT(!gpr_thd_options_is_joinable(&options));
  GPR_ASSERT(gpr_thd_options_is_detached(&options));
}

/* Test that we can create a number of threads and wait for them. */
static void test(void) {
  int i;
  gpr_thd_id thd;
  gpr_thd_id thds[NUM_THREADS];
  struct test t;
  gpr_thd_options options = gpr_thd_options_default();
  gpr_mu_init(&t.mu);
  gpr_cv_init(&t.done_cv);
  t.n = NUM_THREADS;
  t.is_done = 0;
  for (i = 0; i < NUM_THREADS; i++) {
    GPR_ASSERT(gpr_thd_new(&thd, &thd_body, &t, NULL));
  }
  gpr_mu_lock(&t.mu);
  while (!t.is_done) {
    gpr_cv_wait(&t.done_cv, &t.mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&t.mu);
  GPR_ASSERT(t.n == 0);
  gpr_thd_options_set_joinable(&options);
  for (i = 0; i < NUM_THREADS; i++) {
    GPR_ASSERT(gpr_thd_new(&thds[i], &thd_body_joinable, NULL, &options));
  }
  for (i = 0; i < NUM_THREADS; i++) {
    gpr_thd_join(thds[i]);
  }
}

/* ------------------------------------------------- */

int main(int argc, char *argv[]) {
  grpc_test_init(argc, argv);
  test_options();
  test();
  return 0;
}
