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

#include "src/core/lib/surface/completion_queue.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/util/test_config.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x)

static void *create_test_tag(void) {
  static intptr_t i = 0;
  return (void *)(++i);
}

/* helper for tests to shutdown correctly and tersely */
static void shutdown_and_destroy(grpc_completion_queue *cc) {
  grpc_event ev;
  grpc_completion_queue_shutdown(cc);

  switch (grpc_get_cq_completion_type(cc)) {
    case GRPC_CQ_NEXT: {
      ev = grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME),
                                      NULL);
      break;
    }
    case GRPC_CQ_PLUCK: {
      ev = grpc_completion_queue_pluck(cc, create_test_tag(),
                                       gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
      break;
    }
    default: {
      gpr_log(GPR_ERROR, "Unknown completion type");
      break;
    }
  }

  GPR_ASSERT(ev.type == GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cc);
}

static void do_nothing_end_completion(grpc_exec_ctx *exec_ctx, void *arg,
                                      grpc_cq_completion *c) {}

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

  cc = grpc_completion_queue_create_for_pluck(NULL);
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
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(1000));

  ev = grpc_completion_queue_pluck(cc, create_test_tag(),
                                   gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  GPR_ASSERT(ev.type == GRPC_QUEUE_TIMEOUT);

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    GPR_ASSERT(grpc_cq_begin_op(cc, tags[i]));
    grpc_cq_end_op(&exec_ctx, cc, tags[i], GRPC_ERROR_NONE,
                   do_nothing_end_completion, NULL, &completions[i]);
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
  return grpc_timeout_seconds_to_deadline(10);
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
  gpr_event_set(&opt->on_started, (void *)(intptr_t)1);
  GPR_ASSERT(gpr_event_wait(opt->phase1, ten_seconds_time()));

  gpr_log(GPR_INFO, "producer %d phase 1", opt->id);
  for (i = 0; i < TEST_THREAD_EVENTS; i++) {
    GPR_ASSERT(grpc_cq_begin_op(opt->cc, (void *)(intptr_t)1));
  }

  gpr_log(GPR_INFO, "producer %d phase 1 done", opt->id);
  gpr_event_set(&opt->on_phase1_done, (void *)(intptr_t)1);
  GPR_ASSERT(gpr_event_wait(opt->phase2, ten_seconds_time()));

  gpr_log(GPR_INFO, "producer %d phase 2", opt->id);
  for (i = 0; i < TEST_THREAD_EVENTS; i++) {
    grpc_cq_end_op(&exec_ctx, opt->cc, (void *)(intptr_t)1, GRPC_ERROR_NONE,
                   free_completion, NULL,
                   gpr_malloc(sizeof(grpc_cq_completion)));
    opt->events_triggered++;
    grpc_exec_ctx_finish(&exec_ctx);
  }

  gpr_log(GPR_INFO, "producer %d phase 2 done", opt->id);
  gpr_event_set(&opt->on_finished, (void *)(intptr_t)1);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void consumer_thread(void *arg) {
  test_thread_options *opt = arg;
  grpc_event ev;

  gpr_log(GPR_INFO, "consumer %d started", opt->id);
  gpr_event_set(&opt->on_started, (void *)(intptr_t)1);
  GPR_ASSERT(gpr_event_wait(opt->phase1, ten_seconds_time()));

  gpr_log(GPR_INFO, "consumer %d phase 1", opt->id);

  gpr_log(GPR_INFO, "consumer %d phase 1 done", opt->id);
  gpr_event_set(&opt->on_phase1_done, (void *)(intptr_t)1);
  GPR_ASSERT(gpr_event_wait(opt->phase2, ten_seconds_time()));

  gpr_log(GPR_INFO, "consumer %d phase 2", opt->id);
  for (;;) {
    ev = grpc_completion_queue_next(opt->cc,
                                    gpr_inf_future(GPR_CLOCK_MONOTONIC), NULL);
    switch (ev.type) {
      case GRPC_OP_COMPLETE:
        GPR_ASSERT(ev.success);
        opt->events_triggered++;
        break;
      case GRPC_QUEUE_SHUTDOWN:
        gpr_log(GPR_INFO, "consumer %d phase 2 done", opt->id);
        gpr_event_set(&opt->on_finished, (void *)(intptr_t)1);
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
  grpc_completion_queue *cc = grpc_completion_queue_create_for_next(NULL);
  size_t i;
  size_t total_consumed = 0;
  static int optid = 101;

  gpr_log(GPR_INFO, "%s: %" PRIuPTR " producers, %" PRIuPTR " consumers",
          "test_threading", producers, consumers);

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
  gpr_event_set(&phase1, (void *)(intptr_t)1);

  gpr_log(GPR_INFO, "wait phase 1");
  for (i = 0; i < producers + consumers; i++) {
    GPR_ASSERT(gpr_event_wait(&options[i].on_phase1_done, ten_seconds_time()));
  }
  gpr_log(GPR_INFO, "done phase 1");

  /* start phase2: operations will complete, and consumers will consume them */
  gpr_log(GPR_INFO, "start phase 2");
  gpr_event_set(&phase2, (void *)(intptr_t)1);

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
  test_too_many_plucks();
  test_threading(1, 1);
  test_threading(1, 10);
  test_threading(10, 1);
  test_threading(10, 10);
  grpc_shutdown();
  return 0;
}
