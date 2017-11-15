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
static void thd_body(void* v) {
  struct test* t = static_cast<struct test*>(v);
  gpr_mu_lock(&t->mu);
  t->n--;
  if (t->n == 0) {
    t->is_done = 1;
    gpr_cv_signal(&t->done_cv);
  }
  gpr_mu_unlock(&t->mu);
}

static void thd_body_joinable(void* v) {}

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
    GPR_ASSERT(gpr_thd_new(&thd, &thd_body, &t, nullptr));
  }
  gpr_mu_lock(&t.mu);
  while (!t.is_done) {
    gpr_cv_wait(&t.done_cv, &t.mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&t.mu);
  GPR_ASSERT(t.n == 0);
  gpr_thd_options_set_joinable(&options);
  for (i = 0; i < NUM_THREADS; i++) {
    GPR_ASSERT(gpr_thd_new(&thds[i], &thd_body_joinable, nullptr, &options));
  }
  for (i = 0; i < NUM_THREADS; i++) {
    gpr_thd_join(thds[i]);
  }
}

/* ------------------------------------------------- */

int main(int argc, char* argv[]) {
  grpc_test_init(argc, argv);
  test_options();
  test();
  return 0;
}
