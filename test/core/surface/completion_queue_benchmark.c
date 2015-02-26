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

#include "src/core/surface/completion_queue.h"

#include <math.h>
#include <stdio.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

typedef struct test_thread_options {
  gpr_event on_started;
  gpr_event *start;
  gpr_event on_finished;
  grpc_completion_queue *cc;
  int iterations;
} test_thread_options;

static void producer_thread(void *arg) {
  test_thread_options *opt = arg;
  int i;

  gpr_event_set(&opt->on_started, (void *)(gpr_intptr) 1);
  GPR_ASSERT(gpr_event_wait(opt->start, gpr_inf_future));

  for (i = 0; i < opt->iterations; i++) {
    grpc_cq_begin_op(opt->cc, NULL, GRPC_WRITE_ACCEPTED);
    grpc_cq_end_write_accepted(opt->cc, (void *)(gpr_intptr) 1, NULL, NULL,
                               NULL, GRPC_OP_OK);
  }

  gpr_event_set(&opt->on_finished, (void *)(gpr_intptr) 1);
}

static void consumer_thread(void *arg) {
  test_thread_options *opt = arg;
  grpc_event *ev;

  gpr_event_set(&opt->on_started, (void *)(gpr_intptr) 1);
  GPR_ASSERT(gpr_event_wait(opt->start, gpr_inf_future));

  for (;;) {
    ev = grpc_completion_queue_next(opt->cc, gpr_inf_future);
    switch (ev->type) {
      case GRPC_WRITE_ACCEPTED:
        break;
      case GRPC_QUEUE_SHUTDOWN:
        gpr_event_set(&opt->on_finished, (void *)(gpr_intptr) 1);
        return;
      default:
        gpr_log(GPR_ERROR, "Invalid event received: %d", ev->type);
        abort();
    }
    grpc_event_finish(ev);
  }
}

double ops_per_second(int consumers, int producers, int iterations) {
  test_thread_options *options =
      gpr_malloc((producers + consumers) * sizeof(test_thread_options));
  gpr_event start = GPR_EVENT_INIT;
  grpc_completion_queue *cc = grpc_completion_queue_create();
  int i;
  gpr_timespec t_start, t_end, t_delta;

  /* start all threads: they will wait for phase1 */
  for (i = 0; i < producers + consumers; i++) {
    gpr_thd_id id;
    gpr_event_init(&options[i].on_started);
    gpr_event_init(&options[i].on_finished);
    options[i].start = &start;
    options[i].cc = cc;
    options[i].iterations = iterations;
    GPR_ASSERT(gpr_thd_new(&id,
                           i < producers ? producer_thread : consumer_thread,
                           options + i, NULL));
    gpr_event_wait(&options[i].on_started, gpr_inf_future);
  }

  /* start the benchmark */
  t_start = gpr_now();
  gpr_event_set(&start, (void *)(gpr_intptr) 1);

  /* wait for producers to finish */
  for (i = 0; i < producers; i++) {
    GPR_ASSERT(gpr_event_wait(&options[i].on_finished, gpr_inf_future));
  }

  /* in parallel, we shutdown the completion channel - all events should still
     be consumed */
  grpc_completion_queue_shutdown(cc);

  /* join all threads */
  for (i = producers; i < producers + consumers; i++) {
    GPR_ASSERT(gpr_event_wait(&options[i].on_finished, gpr_inf_future));
  }
  t_end = gpr_now();

  /* destroy the completion channel */
  grpc_completion_queue_destroy(cc);

  gpr_free(options);

  t_delta = gpr_time_sub(t_end, t_start);
  return (t_delta.tv_sec + 1e-9 * t_delta.tv_nsec) / (producers * iterations);
}

double ops_per_second_top(int consumers, int producers) {
  return ops_per_second(consumers, producers, 1000000 / producers);
}

int main(void) {
  const int counts[] = {1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 40, 64};
  const int ncounts = sizeof(counts) / sizeof(*counts);
  int i, j;

  printf("\"\",");
  for (i = 0; i < ncounts; i++) {
    int producers = counts[i];
    printf("%d%s", producers, i == ncounts - 1 ? "\n" : ",");
  }

  for (j = 0; j < ncounts; j++) {
    int consumers = counts[j];
    printf("%d,", consumers);
    for (i = 0; i < ncounts; i++) {
      int producers = counts[i];
      printf("%f%s", ops_per_second_top(consumers, producers),
             i == ncounts - 1 ? "\n" : ",");
      fflush(stdout);
    }
  }

  return 0;
}
