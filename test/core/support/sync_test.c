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

/* Test of gpr synchronization support. */

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <stdio.h>
#include <stdlib.h>
#include "test/core/util/test_config.h"

/* ==================Example use of interface===================

   A producer-consumer queue of up to N integers,
   illustrating the use of the calls in this interface.  */

#define N 4

typedef struct queue {
  gpr_cv non_empty; /* Signalled when length becomes non-zero. */
  gpr_cv non_full;  /* Signalled when length becomes non-N. */
  gpr_mu mu;        /* Protects all fields below.
                       (That is, except during initialization or
                       destruction, the fields below should be accessed
                       only by a thread that holds mu.) */
  int head;         /* Index of head of queue 0..N-1. */
  int length;       /* Number of valid elements in queue 0..N. */
  int elem[N];      /* elem[head .. head+length-1] are queue elements. */
} queue;

/* Initialize *q. */
void queue_init(queue *q) {
  gpr_mu_init(&q->mu);
  gpr_cv_init(&q->non_empty);
  gpr_cv_init(&q->non_full);
  q->head = 0;
  q->length = 0;
}

/* Free storage associated with *q. */
void queue_destroy(queue *q) {
  gpr_mu_destroy(&q->mu);
  gpr_cv_destroy(&q->non_empty);
  gpr_cv_destroy(&q->non_full);
}

/* Wait until there is room in *q, then append x to *q. */
void queue_append(queue *q, int x) {
  gpr_mu_lock(&q->mu);
  /* To wait for a predicate without a deadline, loop on the negation of the
     predicate, and use gpr_cv_wait(..., gpr_inf_future(GPR_CLOCK_REALTIME))
     inside the loop
     to release the lock, wait, and reacquire on each iteration.  Code that
     makes the condition true should use gpr_cv_broadcast() on the
     corresponding condition variable.  The predicate must be on state
     protected by the lock.  */
  while (q->length == N) {
    gpr_cv_wait(&q->non_full, &q->mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  if (q->length == 0) { /* Wake threads blocked in queue_remove(). */
    /* It's normal to use gpr_cv_broadcast() or gpr_signal() while
       holding the lock. */
    gpr_cv_broadcast(&q->non_empty);
  }
  q->elem[(q->head + q->length) % N] = x;
  q->length++;
  gpr_mu_unlock(&q->mu);
}

/* If it can be done without blocking, append x to *q and return non-zero.
   Otherwise return 0. */
int queue_try_append(queue *q, int x) {
  int result = 0;
  if (gpr_mu_trylock(&q->mu)) {
    if (q->length != N) {
      if (q->length == 0) { /* Wake threads blocked in queue_remove(). */
        gpr_cv_broadcast(&q->non_empty);
      }
      q->elem[(q->head + q->length) % N] = x;
      q->length++;
      result = 1;
    }
    gpr_mu_unlock(&q->mu);
  }
  return result;
}

/* Wait until the *q is non-empty or deadline abs_deadline passes.  If the
   queue is non-empty, remove its head entry, place it in *head, and return
   non-zero.  Otherwise return 0.  */
int queue_remove(queue *q, int *head, gpr_timespec abs_deadline) {
  int result = 0;
  gpr_mu_lock(&q->mu);
  /* To wait for a predicate with a deadline, loop on the negation of the
     predicate or until gpr_cv_wait() returns true.  Code that makes
     the condition true should use gpr_cv_broadcast() on the corresponding
     condition variable.  The predicate must be on state protected by the
     lock. */
  while (q->length == 0 && !gpr_cv_wait(&q->non_empty, &q->mu, abs_deadline)) {
  }
  if (q->length != 0) { /* Queue is non-empty. */
    result = 1;
    if (q->length == N) { /* Wake threads blocked in queue_append(). */
      gpr_cv_broadcast(&q->non_full);
    }
    *head = q->elem[q->head];
    q->head = (q->head + 1) % N;
    q->length--;
  } /* else deadline exceeded */
  gpr_mu_unlock(&q->mu);
  return result;
}

/* ------------------------------------------------- */
/* Tests for gpr_mu and gpr_cv, and the queue example. */
struct test {
  int threads; /* number of threads */

  int64_t iterations; /* number of iterations per thread */
  int64_t counter;
  int thread_count; /* used to allocate thread ids */
  int done;         /* threads not yet completed */
  int incr_step;    /* how much to increment/decrement refcount each time */

  gpr_mu mu; /* protects iterations, counter, thread_count, done */

  gpr_cv cv; /* signalling depends on test */

  gpr_cv done_cv; /* signalled when done == 0 */

  queue q;

  gpr_stats_counter stats_counter;

  gpr_refcount refcount;
  gpr_refcount thread_refcount;
  gpr_event event;
};

/* Return pointer to a new struct test. */
static struct test *test_new(int threads, int64_t iterations, int incr_step) {
  struct test *m = gpr_malloc(sizeof(*m));
  m->threads = threads;
  m->iterations = iterations;
  m->counter = 0;
  m->thread_count = 0;
  m->done = threads;
  m->incr_step = incr_step;
  gpr_mu_init(&m->mu);
  gpr_cv_init(&m->cv);
  gpr_cv_init(&m->done_cv);
  queue_init(&m->q);
  gpr_stats_init(&m->stats_counter, 0);
  gpr_ref_init(&m->refcount, 0);
  gpr_ref_init(&m->thread_refcount, threads);
  gpr_event_init(&m->event);
  return m;
}

/* Return pointer to a new struct test. */
static void test_destroy(struct test *m) {
  gpr_mu_destroy(&m->mu);
  gpr_cv_destroy(&m->cv);
  gpr_cv_destroy(&m->done_cv);
  queue_destroy(&m->q);
  gpr_free(m);
}

/* Create m->threads threads, each running (*body)(m) */
static void test_create_threads(struct test *m, void (*body)(void *arg)) {
  gpr_thd_id id;
  int i;
  for (i = 0; i != m->threads; i++) {
    GPR_ASSERT(gpr_thd_new(&id, body, m, NULL));
  }
}

/* Wait until all threads report done. */
static void test_wait(struct test *m) {
  gpr_mu_lock(&m->mu);
  while (m->done != 0) {
    gpr_cv_wait(&m->done_cv, &m->mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&m->mu);
}

/* Get an integer thread id in the raneg 0..threads-1 */
static int thread_id(struct test *m) {
  int id;
  gpr_mu_lock(&m->mu);
  id = m->thread_count++;
  gpr_mu_unlock(&m->mu);
  return id;
}

/* Indicate that a thread is done, by decrementing m->done
   and signalling done_cv if m->done==0. */
static void mark_thread_done(struct test *m) {
  gpr_mu_lock(&m->mu);
  GPR_ASSERT(m->done != 0);
  m->done--;
  if (m->done == 0) {
    gpr_cv_signal(&m->done_cv);
  }
  gpr_mu_unlock(&m->mu);
}

/* Test several threads running (*body)(struct test *m) for increasing settings
   of m->iterations, until about timeout_s to 2*timeout_s seconds have elapsed.
   If extra!=NULL, run (*extra)(m) in an additional thread.
   incr_step controls by how much m->refcount should be incremented/decremented
   (if at all) each time in the tests.
   */
static void test(const char *name, void (*body)(void *m),
                 void (*extra)(void *m), int timeout_s, int incr_step) {
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
    if (extra != NULL) {
      gpr_thd_id id;
      GPR_ASSERT(gpr_thd_new(&id, extra, m, NULL));
      m->done++; /* one more thread to wait for */
    }
    test_create_threads(m, body);
    test_wait(m);
    if (m->counter != m->threads * m->iterations * m->incr_step) {
      fprintf(stderr, "counter %ld  threads %d  iterations %ld\n",
              (long)m->counter, m->threads, (long)m->iterations);
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
    gpr_mu_lock(&m->mu);
    m->counter++;
    gpr_mu_unlock(&m->mu);
  }
  mark_thread_done(m);
}

/* Increment m->counter under lock acquired with trylock, m->iterations times;
   then mark thread as done.  */
static void inctry(void *v /*=m*/) {
  struct test *m = v;
  int64_t i;
  for (i = 0; i != m->iterations;) {
    if (gpr_mu_trylock(&m->mu)) {
      m->counter++;
      gpr_mu_unlock(&m->mu);
      i++;
    }
  }
  mark_thread_done(m);
}

/* Increment counter only when (m->counter%m->threads)==m->thread_id; then mark
   thread as done.  */
static void inc_by_turns(void *v /*=m*/) {
  struct test *m = v;
  int64_t i;
  int id = thread_id(m);
  for (i = 0; i != m->iterations; i++) {
    gpr_mu_lock(&m->mu);
    while ((m->counter % m->threads) != id) {
      gpr_cv_wait(&m->cv, &m->mu, gpr_inf_future(GPR_CLOCK_REALTIME));
    }
    m->counter++;
    gpr_cv_broadcast(&m->cv);
    gpr_mu_unlock(&m->mu);
  }
  mark_thread_done(m);
}

/* Wait a millisecond and increment counter on each iteration;
   then mark thread as done. */
static void inc_with_1ms_delay(void *v /*=m*/) {
  struct test *m = v;
  int64_t i;
  for (i = 0; i != m->iterations; i++) {
    gpr_timespec deadline;
    gpr_mu_lock(&m->mu);
    deadline = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                            gpr_time_from_micros(1000, GPR_TIMESPAN));
    while (!gpr_cv_wait(&m->cv, &m->mu, deadline)) {
    }
    m->counter++;
    gpr_mu_unlock(&m->mu);
  }
  mark_thread_done(m);
}

/* Wait a millisecond and increment counter on each iteration, using an event
   for timing; then mark thread as done. */
static void inc_with_1ms_delay_event(void *v /*=m*/) {
  struct test *m = v;
  int64_t i;
  for (i = 0; i != m->iterations; i++) {
    gpr_timespec deadline;
    deadline = gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                            gpr_time_from_micros(1000, GPR_TIMESPAN));
    GPR_ASSERT(gpr_event_wait(&m->event, deadline) == NULL);
    gpr_mu_lock(&m->mu);
    m->counter++;
    gpr_mu_unlock(&m->mu);
  }
  mark_thread_done(m);
}

/* Produce m->iterations elements on queue m->q, then mark thread as done.
   Even threads use queue_append(), and odd threads use queue_try_append()
   until it succeeds. */
static void many_producers(void *v /*=m*/) {
  struct test *m = v;
  int64_t i;
  int x = thread_id(m);
  if ((x & 1) == 0) {
    for (i = 0; i != m->iterations; i++) {
      queue_append(&m->q, 1);
    }
  } else {
    for (i = 0; i != m->iterations; i++) {
      while (!queue_try_append(&m->q, 1)) {
      }
    }
  }
  mark_thread_done(m);
}

/* Consume elements from m->q until m->threads*m->iterations are seen,
   wait an extra second to confirm that no more elements are arriving,
   then mark thread as done. */
static void consumer(void *v /*=m*/) {
  struct test *m = v;
  int64_t n = m->iterations * m->threads;
  int64_t i;
  int value;
  for (i = 0; i != n; i++) {
    queue_remove(&m->q, &value, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_lock(&m->mu);
  m->counter = n;
  gpr_mu_unlock(&m->mu);
  GPR_ASSERT(
      !queue_remove(&m->q, &value,
                    gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_micros(1000000, GPR_TIMESPAN))));
  mark_thread_done(m);
}

/* Increment m->stats_counter m->iterations times, transfer counter value to
   m->counter, then mark thread as done.  */
static void statsinc(void *v /*=m*/) {
  struct test *m = v;
  int64_t i;
  for (i = 0; i != m->iterations; i++) {
    gpr_stats_inc(&m->stats_counter, 1);
  }
  gpr_mu_lock(&m->mu);
  m->counter = gpr_stats_read(&m->stats_counter);
  gpr_mu_unlock(&m->mu);
  mark_thread_done(m);
}

/* Increment m->refcount by m->incr_step for m->iterations times. Decrement
   m->thread_refcount once, and if it reaches zero, set m->event to (void*)1;
   then mark thread as done.  */
static void refinc(void *v /*=m*/) {
  struct test *m = v;
  int64_t i;
  for (i = 0; i != m->iterations; i++) {
    if (m->incr_step == 1) {
      gpr_ref(&m->refcount);
    } else {
      gpr_refn(&m->refcount, m->incr_step);
    }
  }
  if (gpr_unref(&m->thread_refcount)) {
    gpr_event_set(&m->event, (void *)1);
  }
  mark_thread_done(m);
}

/* Wait until m->event is set to (void *)1, then decrement m->refcount by 1
   (m->threads * m->iterations * m->incr_step) times, and ensure that the last
   decrement caused the counter to reach zero, then mark thread as done.  */
static void refcheck(void *v /*=m*/) {
  struct test *m = v;
  int64_t n = m->iterations * m->threads * m->incr_step;
  int64_t i;
  GPR_ASSERT(gpr_event_wait(&m->event, gpr_inf_future(GPR_CLOCK_REALTIME)) ==
             (void *)1);
  GPR_ASSERT(gpr_event_get(&m->event) == (void *)1);
  for (i = 1; i != n; i++) {
    GPR_ASSERT(!gpr_unref(&m->refcount));
    m->counter++;
  }
  GPR_ASSERT(gpr_unref(&m->refcount));
  m->counter++;
  mark_thread_done(m);
}

/* ------------------------------------------------- */

int main(int argc, char *argv[]) {
  grpc_test_init(argc, argv);
  test("mutex", &inc, NULL, 1, 1);
  test("mutex try", &inctry, NULL, 1, 1);
  test("cv", &inc_by_turns, NULL, 1, 1);
  test("timedcv", &inc_with_1ms_delay, NULL, 1, 1);
  test("queue", &many_producers, &consumer, 10, 1);
  test("stats_counter", &statsinc, NULL, 1, 1);
  test("refcount by 1", &refinc, &refcheck, 1, 1);
  test("refcount by 3", &refinc, &refcheck, 1, 3); /* incr_step of 3 is an
                                                      arbitrary choice. Any
                                                      number > 1 is okay here */
  test("timedevent", &inc_with_1ms_delay_event, NULL, 1, 1);
  return 0;
}
