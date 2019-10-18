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

#include "src/core/lib/gprpp/thd.h"

#include <stdio.h>
#include <stdlib.h>

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "test/core/util/test_config.h"

#define NUM_THREADS 100

struct test {
  gpr_mu mu;
  int n;
  int is_done;
  gpr_cv done_cv;
};

/* A Thread body.   Decrement t->n, and if is becomes zero, set t->done. */
static void thd_body1(void* v) {
  struct test* t = static_cast<struct test*>(v);
  gpr_mu_lock(&t->mu);
  t->n--;
  if (t->n == 0) {
    t->is_done = 1;
    gpr_cv_signal(&t->done_cv);
  }
  gpr_mu_unlock(&t->mu);
}

/* Test that we can create a number of threads, wait for them, and join them. */
static void test1(void) {
  grpc_core::Thread thds[NUM_THREADS];
  struct test t;
  gpr_mu_init(&t.mu);
  gpr_cv_init(&t.done_cv);
  t.n = NUM_THREADS;
  t.is_done = 0;
  for (auto& th : thds) {
    th = grpc_core::Thread("grpc_thread_body1_test", &thd_body1, &t);
    th.Start();
  }
  gpr_mu_lock(&t.mu);
  while (!t.is_done) {
    gpr_cv_wait(&t.done_cv, &t.mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&t.mu);
  for (auto& th : thds) {
    th.Join();
  }
  GPR_ASSERT(t.n == 0);
  gpr_mu_destroy(&t.mu);
  gpr_cv_destroy(&t.done_cv);
}

static void thd_body2(void* /*v*/) {}

/* Test that we can create a number of threads and join them. */
static void test2(void) {
  grpc_core::Thread thds[NUM_THREADS];
  for (auto& th : thds) {
    bool ok;
    th = grpc_core::Thread("grpc_thread_body2_test", &thd_body2, nullptr, &ok);
    GPR_ASSERT(ok);
    th.Start();
  }
  for (auto& th : thds) {
    th.Join();
  }
}

/* ------------------------------------------------- */

int main(int argc, char* argv[]) {
  grpc::testing::TestEnvironment env(argc, argv);
  test1();
  test2();
  return 0;
}
