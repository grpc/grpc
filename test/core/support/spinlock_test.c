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

/* Test of gpr synchronization support. */

#include "src/core/lib/support/spinlock.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <stdio.h>
#include <stdlib.h>
#include "test/core/util/test_config.h"

/* ------------------------------------------------- */
/* Tests for gpr_spinlock. */
struct test {
  int thread_count; /* number of threads */
  gpr_thd_id *threads;

  int64_t iterations; /* number of iterations per thread */
  int64_t counter;
  int incr_step; /* how much to increment/decrement refcount each time */

  gpr_spinlock mu; /* protects iterations, counter */
};

/* Return pointer to a new struct test. */
static struct test *test_new(int threads, int64_t iterations, int incr_step) {
  struct test *m = gpr_malloc(sizeof(*m));
  m->thread_count = threads;
  m->threads = gpr_malloc(sizeof(*m->threads) * (size_t)threads);
  m->iterations = iterations;
  m->counter = 0;
  m->thread_count = 0;
  m->incr_step = incr_step;
  m->mu = GPR_SPINLOCK_INITIALIZER;
  return m;
}

/* Return pointer to a new struct test. */
static void test_destroy(struct test *m) {
  gpr_free(m->threads);
  gpr_free(m);
}

/* Create m->threads threads, each running (*body)(m) */
static void test_create_threads(struct test *m, void (*body)(void *arg)) {
  int i;
  for (i = 0; i != m->thread_count; i++) {
    gpr_thd_options opt = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&opt);
    GPR_ASSERT(gpr_thd_new(&m->threads[i], body, m, &opt));
  }
}

/* Wait until all threads report done. */
static void test_wait(struct test *m) {
  int i;
  for (i = 0; i != m->thread_count; i++) {
    gpr_thd_join(m->threads[i]);
  }
}

/* Test several threads running (*body)(struct test *m) for increasing settings
   of m->iterations, until about timeout_s to 2*timeout_s seconds have elapsed.
   If extra!=NULL, run (*extra)(m) in an additional thread.
   incr_step controls by how much m->refcount should be incremented/decremented
   (if at all) each time in the tests.
   */
static void test(const char *name, void (*body)(void *m), int timeout_s,
                 int incr_step) {
  int64_t iterations = 1024;
  struct test *m;
  gpr_timespec start = gpr_now(GPR_CLOCK_REALTIME);
  gpr_timespec time_taken;
  gpr_timespec deadline = gpr_time_add(
      start, gpr_time_from_micros((int64_t)timeout_s * 1000000, GPR_TIMESPAN));
  fprintf(stderr, "%s:", name);
  while (gpr_time_cmp(gpr_now(GPR_CLOCK_REALTIME), deadline) < 0) {
    iterations <<= 1;
    fprintf(stderr, " %ld", (long)iterations);
    m = test_new(10, iterations, incr_step);
    test_create_threads(m, body);
    test_wait(m);
    if (m->counter != m->thread_count * m->iterations * m->incr_step) {
      fprintf(stderr, "counter %ld  threads %d  iterations %ld\n",
              (long)m->counter, m->thread_count, (long)m->iterations);
      GPR_ASSERT(0);
    }
    test_destroy(m);
  }
  time_taken = gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), start);
  fprintf(stderr, " done %lld.%09d s\n", (long long)time_taken.tv_sec,
          (int)time_taken.tv_nsec);
}

/* Increment m->counter on each iteration; then mark thread as done.  */
static void inc(void *v /*=m*/) {
  struct test *m = v;
  int64_t i;
  for (i = 0; i != m->iterations; i++) {
    gpr_spinlock_lock(&m->mu);
    m->counter++;
    gpr_spinlock_unlock(&m->mu);
  }
}

/* Increment m->counter under lock acquired with trylock, m->iterations times;
   then mark thread as done.  */
static void inctry(void *v /*=m*/) {
  struct test *m = v;
  int64_t i;
  for (i = 0; i != m->iterations;) {
    if (gpr_spinlock_trylock(&m->mu)) {
      m->counter++;
      gpr_spinlock_unlock(&m->mu);
      i++;
    }
  }
}

/* ------------------------------------------------- */

int main(int argc, char *argv[]) {
  grpc_test_init(argc, argv);
  test("spinlock", &inc, 1, 1);
  test("spinlock try", &inctry, 1, 1);
  return 0;
}
