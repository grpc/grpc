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

#include "src/core/iomgr/iomgr.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x)

static void *create_test_tag(void) {
  static gpr_intptr i = 0;
  return (void *)(++i);
}

/* helper for tests to shutdown correctly and tersely */
static void shutdown_and_destroy(grpc_completion_queue *cc) {
  grpc_event ev;
  grpc_completion_queue_shutdown(cc);
  ev = grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
  GPR_ASSERT(ev.type == GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cc);
}

/* ensure we can create and destroy a completion channel */
static void test_no_op(void) {
  LOG_TEST("test_no_op");
  shutdown_and_destroy(grpc_completion_queue_create(NULL));
}

static void test_wait_empty(void) {
  grpc_completion_queue *cc;
  grpc_event event;

  LOG_TEST("test_wait_empty");

  cc = grpc_completion_queue_create(NULL);
  event = grpc_completion_queue_next(cc, gpr_now(GPR_CLOCK_REALTIME), NULL);
  GPR_ASSERT(event.type == GRPC_QUEUE_TIMEOUT);
  shutdown_and_destroy(cc);
}

static void do_nothing_end_completion(grpc_exec_ctx *exec_ctx, void *arg,
                                      grpc_cq_completion *c) {}

static void test_cq_end_op(void) {
  grpc_event ev;
  grpc_completion_queue *cc;
  grpc_cq_completion completion;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  void *tag = create_test_tag();

  LOG_TEST("test_cq_end_op");

  cc = grpc_completion_queue_create(NULL);

  grpc_cq_begin_op(cc, tag);
  grpc_cq_end_op(&exec_ctx, cc, tag, 1, do_nothing_end_completion, NULL,
                 &completion);

  ev = grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(ev.tag == tag);
  GPR_ASSERT(ev.success);

  shutdown_and_destroy(cc);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_shutdown_then_next_polling(void) {
  grpc_completion_queue *cc;
  grpc_event event;
  LOG_TEST("test_shutdown_then_next_polling");

  cc = grpc_completion_queue_create(NULL);
  grpc_completion_queue_shutdown(cc);
  event =
      grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
  GPR_ASSERT(event.type == GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cc);
}

static void test_shutdown_then_next_with_timeout(void) {
  grpc_completion_queue *cc;
  grpc_event event;
  LOG_TEST("test_shutdown_then_next_with_timeout");

  cc = grpc_completion_queue_create(NULL);
  grpc_completion_queue_shutdown(cc);
  event =
      grpc_completion_queue_next(cc, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  GPR_ASSERT(event.type == GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cc);
}

static void test_pluck(void) {
  grpc_event ev;
  grpc_completion_queue *cc;
  void *tags[128];
  grpc_cq_completion completions[GPR_ARRAY_SIZE(tags)];
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  unsigned i, j;

  LOG_TEST("test_pluck");

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    tags[i] = create_test_tag();
    for (j = 0; j < i; j++) {
      GPR_ASSERT(tags[i] != tags[j]);
    }
  }

  cc = grpc_completion_queue_create(NULL);

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    grpc_cq_begin_op(cc, tags[i]);
    grpc_cq_end_op(&exec_ctx, cc, tags[i], 1, do_nothing_end_completion, NULL,
                   &completions[i]);
  }

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    ev = grpc_completion_queue_pluck(cc, tags[i],
                                     gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
    GPR_ASSERT(ev.tag == tags[i]);
  }

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    grpc_cq_begin_op(cc, tags[i]);
    grpc_cq_end_op(&exec_ctx, cc, tags[i], 1, do_nothing_end_completion, NULL,
                   &completions[i]);
  }

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    ev = grpc_completion_queue_pluck(cc, tags[GPR_ARRAY_SIZE(tags) - i - 1],
                                     gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
    GPR_ASSERT(ev.tag == tags[GPR_ARRAY_SIZE(tags) - i - 1]);
  }

  shutdown_and_destroy(cc);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_pluck_after_shutdown(void) {
  grpc_event ev;
  grpc_completion_queue *cc;

  LOG_TEST("test_pluck_after_shutdown");
  cc = grpc_completion_queue_create(NULL);
  grpc_completion_queue_shutdown(cc);
  ev = grpc_completion_queue_pluck(cc, NULL, gpr_inf_future(GPR_CLOCK_REALTIME),
                                   NULL);
  GPR_ASSERT(ev.type == GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cc);
}

struct thread_state {
  grpc_completion_queue *cc;
  void *tag;
};

static void pluck_one(void *arg) {
  struct thread_state *state = arg;
  grpc_completion_queue_pluck(state->cc, state->tag,
                              gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
}

static void test_too_many_plucks(void) {
  grpc_event ev;
  grpc_completion_queue *cc;
  void *tags[GRPC_MAX_COMPLETION_QUEUE_PLUCKERS];
  grpc_cq_completion completions[GPR_ARRAY_SIZE(tags)];
  gpr_thd_id thread_ids[GPR_ARRAY_SIZE(tags)];
  struct thread_state thread_states[GPR_ARRAY_SIZE(tags)];
  gpr_thd_options thread_options = gpr_thd_options_default();
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  unsigned i, j;

  LOG_TEST("test_too_many_plucks");

  cc = grpc_completion_queue_create(NULL);
  gpr_thd_options_set_joinable(&thread_options);

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    tags[i] = create_test_tag();
    for (j = 0; j < i; j++) {
      GPR_ASSERT(tags[i] != tags[j]);
    }
    thread_states[i].cc = cc;
    thread_states[i].tag = tags[i];
    gpr_thd_new(thread_ids + i, pluck_one, thread_states + i, &thread_options);
  }

  /* wait until all other threads are plucking */
  gpr_sleep_until(GRPC_TIMEOUT_MILLIS_TO_DEADLINE(100));

  ev = grpc_completion_queue_pluck(cc, create_test_tag(),
                                   gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  GPR_ASSERT(ev.type == GRPC_QUEUE_TIMEOUT);

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    grpc_cq_begin_op(cc, tags[i]);
    grpc_cq_end_op(&exec_ctx, cc, tags[i], 1, do_nothing_end_completion, NULL,
                   &completions[i]);
  }

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    gpr_thd_join(thread_ids[i]);
  }

  shutdown_and_destroy(cc);
  grpc_exec_ctx_finish(&exec_ctx);
}

#define TEST_THREAD_EVENTS 10000

typedef struct test_thread_options {
  gpr_event on_started;
  gpr_event *phase1;
  gpr_event on_phase1_done;
  gpr_event *phase2;
  gpr_event on_finished;
  size_t events_triggered;
  int id;
  grpc_completion_queue *cc;
} test_thread_options;

gpr_timespec ten_seconds_time(void) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(10);
}

static void free_completion(grpc_exec_ctx *exec_ctx, void *arg,
                            grpc_cq_completion *completion) {
  gpr_free(completion);
}

static void producer_thread(void *arg) {
  test_thread_options *opt = arg;
  int i;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  gpr_log(GPR_INFO, "producer %d started", opt->id);
  gpr_event_set(&opt->on_started, (void *)(gpr_intptr)1);
  GPR_ASSERT(gpr_event_wait(opt->phase1, ten_seconds_time()));

  gpr_log(GPR_INFO, "producer %d phase 1", opt->id);
  for (i = 0; i < TEST_THREAD_EVENTS; i++) {
    grpc_cq_begin_op(opt->cc, (void *)(gpr_intptr)1);
  }

  gpr_log(GPR_INFO, "producer %d phase 1 done", opt->id);
  gpr_event_set(&opt->on_phase1_done, (void *)(gpr_intptr)1);
  GPR_ASSERT(gpr_event_wait(opt->phase2, ten_seconds_time()));

  gpr_log(GPR_INFO, "producer %d phase 2", opt->id);
  for (i = 0; i < TEST_THREAD_EVENTS; i++) {
    grpc_cq_end_op(&exec_ctx, opt->cc, (void *)(gpr_intptr)1, 1,
                   free_completion, NULL,
                   gpr_malloc(sizeof(grpc_cq_completion)));
    opt->events_triggered++;
    grpc_exec_ctx_finish(&exec_ctx);
  }

  gpr_log(GPR_INFO, "producer %d phase 2 done", opt->id);
  gpr_event_set(&opt->on_finished, (void *)(gpr_intptr)1);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void consumer_thread(void *arg) {
  test_thread_options *opt = arg;
  grpc_event ev;

  gpr_log(GPR_INFO, "consumer %d started", opt->id);
  gpr_event_set(&opt->on_started, (void *)(gpr_intptr)1);
  GPR_ASSERT(gpr_event_wait(opt->phase1, ten_seconds_time()));

  gpr_log(GPR_INFO, "consumer %d phase 1", opt->id);

  gpr_log(GPR_INFO, "consumer %d phase 1 done", opt->id);
  gpr_event_set(&opt->on_phase1_done, (void *)(gpr_intptr)1);
  GPR_ASSERT(gpr_event_wait(opt->phase2, ten_seconds_time()));

  gpr_log(GPR_INFO, "consumer %d phase 2", opt->id);
  for (;;) {
    ev = grpc_completion_queue_next(opt->cc, ten_seconds_time(), NULL);
    switch (ev.type) {
      case GRPC_OP_COMPLETE:
        GPR_ASSERT(ev.success);
        opt->events_triggered++;
        break;
      case GRPC_QUEUE_SHUTDOWN:
        gpr_log(GPR_INFO, "consumer %d phase 2 done", opt->id);
        gpr_event_set(&opt->on_finished, (void *)(gpr_intptr)1);
        return;
      case GRPC_QUEUE_TIMEOUT:
        gpr_log(GPR_ERROR, "Invalid timeout received");
        abort();
    }
  }
}

static void test_threading(size_t producers, size_t consumers) {
  test_thread_options *options =
      gpr_malloc((producers + consumers) * sizeof(test_thread_options));
  gpr_event phase1 = GPR_EVENT_INIT;
  gpr_event phase2 = GPR_EVENT_INIT;
  grpc_completion_queue *cc = grpc_completion_queue_create(NULL);
  size_t i;
  size_t total_consumed = 0;
  static int optid = 101;

  gpr_log(GPR_INFO, "%s: %d producers, %d consumers", "test_threading",
          producers, consumers);

  /* start all threads: they will wait for phase1 */
  for (i = 0; i < producers + consumers; i++) {
    gpr_thd_id id;
    gpr_event_init(&options[i].on_started);
    gpr_event_init(&options[i].on_phase1_done);
    gpr_event_init(&options[i].on_finished);
    options[i].phase1 = &phase1;
    options[i].phase2 = &phase2;
    options[i].events_triggered = 0;
    options[i].cc = cc;
    options[i].id = optid++;
    GPR_ASSERT(gpr_thd_new(&id,
                           i < producers ? producer_thread : consumer_thread,
                           options + i, NULL));
    gpr_event_wait(&options[i].on_started, ten_seconds_time());
  }

  /* start phase1: producers will pre-declare all operations they will
     complete */
  gpr_log(GPR_INFO, "start phase 1");
  gpr_event_set(&phase1, (void *)(gpr_intptr)1);

  gpr_log(GPR_INFO, "wait phase 1");
  for (i = 0; i < producers + consumers; i++) {
    GPR_ASSERT(gpr_event_wait(&options[i].on_phase1_done, ten_seconds_time()));
  }
  gpr_log(GPR_INFO, "done phase 1");

  /* start phase2: operations will complete, and consumers will consume them */
  gpr_log(GPR_INFO, "start phase 2");
  gpr_event_set(&phase2, (void *)(gpr_intptr)1);

  /* in parallel, we shutdown the completion channel - all events should still
     be consumed */
  grpc_completion_queue_shutdown(cc);

  /* join all threads */
  gpr_log(GPR_INFO, "wait phase 2");
  for (i = 0; i < producers + consumers; i++) {
    GPR_ASSERT(gpr_event_wait(&options[i].on_finished, ten_seconds_time()));
  }
  gpr_log(GPR_INFO, "done phase 2");

  /* destroy the completion channel */
  grpc_completion_queue_destroy(cc);

  /* verify that everything was produced and consumed */
  for (i = 0; i < producers + consumers; i++) {
    if (i < producers) {
      GPR_ASSERT(options[i].events_triggered == TEST_THREAD_EVENTS);
    } else {
      total_consumed += options[i].events_triggered;
    }
  }
  GPR_ASSERT(total_consumed == producers * TEST_THREAD_EVENTS);

  gpr_free(options);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_no_op();
  test_wait_empty();
  test_shutdown_then_next_polling();
  test_shutdown_then_next_with_timeout();
  test_cq_end_op();
  test_pluck();
  test_pluck_after_shutdown();
  test_too_many_plucks();
  test_threading(1, 1);
  test_threading(1, 10);
  test_threading(10, 1);
  test_threading(10, 10);
  grpc_shutdown();
  return 0;
}
