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

/* ensure we can create and destroy a completion channel */
static void test_no_op(void) {
  grpc_cq_completion_type completion_types[] = {GRPC_CQ_NEXT, GRPC_CQ_PLUCK};
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue_attributes attr;
  LOG_TEST("test_no_op");

  attr.version = 1;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(completion_types); i++) {
    for (size_t j = 0; j < GPR_ARRAY_SIZE(polling_types); j++) {
      attr.cq_completion_type = completion_types[i];
      attr.cq_polling_type = polling_types[j];
      shutdown_and_destroy(grpc_completion_queue_create(
          grpc_completion_queue_factory_lookup(&attr), &attr, NULL));
    }
  }
}

static void test_pollset_conversion(void) {
  grpc_cq_completion_type completion_types[] = {GRPC_CQ_NEXT, GRPC_CQ_PLUCK};
  grpc_cq_polling_type polling_types[] = {GRPC_CQ_DEFAULT_POLLING,
                                          GRPC_CQ_NON_LISTENING};
  grpc_completion_queue *cq;
  grpc_completion_queue_attributes attr;

  LOG_TEST("test_pollset_conversion");

  attr.version = 1;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(completion_types); i++) {
    for (size_t j = 0; j < GPR_ARRAY_SIZE(polling_types); j++) {
      attr.cq_completion_type = completion_types[i];
      attr.cq_polling_type = polling_types[j];
      cq = grpc_completion_queue_create(
          grpc_completion_queue_factory_lookup(&attr), &attr, NULL);
      GPR_ASSERT(grpc_cq_pollset(cq) != NULL);
      shutdown_and_destroy(cq);
    }
  }
}

static void test_wait_empty(void) {
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue *cc;
  grpc_completion_queue_attributes attr;
  grpc_event event;

  LOG_TEST("test_wait_empty");

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_NEXT;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(polling_types); i++) {
    attr.cq_polling_type = polling_types[i];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, NULL);
    event = grpc_completion_queue_next(cc, gpr_now(GPR_CLOCK_REALTIME), NULL);
    GPR_ASSERT(event.type == GRPC_QUEUE_TIMEOUT);
    shutdown_and_destroy(cc);
  }
}

static void do_nothing_end_completion(grpc_exec_ctx *exec_ctx, void *arg,
                                      grpc_cq_completion *c) {}

static void test_cq_end_op(void) {
  grpc_event ev;
  grpc_completion_queue *cc;
  grpc_cq_completion completion;
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue_attributes attr;
  grpc_exec_ctx init_exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_exec_ctx exec_ctx;
  void *tag = create_test_tag();

  LOG_TEST("test_cq_end_op");

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_NEXT;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(polling_types); i++) {
    exec_ctx = init_exec_ctx;  // Reset exec_ctx
    attr.cq_polling_type = polling_types[i];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, NULL);

    GPR_ASSERT(grpc_cq_begin_op(cc, tag));
    grpc_cq_end_op(&exec_ctx, cc, tag, GRPC_ERROR_NONE,
                   do_nothing_end_completion, NULL, &completion);

    ev = grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(ev.tag == tag);
    GPR_ASSERT(ev.success);

    shutdown_and_destroy(cc);
    grpc_exec_ctx_finish(&exec_ctx);
  }
}

static void test_shutdown_then_next_polling(void) {
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue *cc;
  grpc_completion_queue_attributes attr;
  grpc_event event;
  LOG_TEST("test_shutdown_then_next_polling");

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_NEXT;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(polling_types); i++) {
    attr.cq_polling_type = polling_types[i];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, NULL);
    grpc_completion_queue_shutdown(cc);
    event =
        grpc_completion_queue_next(cc, gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
    GPR_ASSERT(event.type == GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cc);
  }
}

static void test_shutdown_then_next_with_timeout(void) {
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue *cc;
  grpc_completion_queue_attributes attr;
  grpc_event event;
  LOG_TEST("test_shutdown_then_next_with_timeout");

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_NEXT;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(polling_types); i++) {
    attr.cq_polling_type = polling_types[i];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, NULL);

    grpc_completion_queue_shutdown(cc);
    event = grpc_completion_queue_next(cc, gpr_inf_future(GPR_CLOCK_REALTIME),
                                       NULL);
    GPR_ASSERT(event.type == GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cc);
  }
}

static void test_pluck(void) {
  grpc_event ev;
  grpc_completion_queue *cc;
  void *tags[128];
  grpc_cq_completion completions[GPR_ARRAY_SIZE(tags)];
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_completion_queue_attributes attr;
  grpc_exec_ctx init_exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_exec_ctx exec_ctx;
  unsigned i, j;

  LOG_TEST("test_pluck");

  for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
    tags[i] = create_test_tag();
    for (j = 0; j < i; j++) {
      GPR_ASSERT(tags[i] != tags[j]);
    }
  }

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_PLUCK;
  for (size_t pidx = 0; pidx < GPR_ARRAY_SIZE(polling_types); pidx++) {
    exec_ctx = init_exec_ctx;  // reset exec_ctx
    attr.cq_polling_type = polling_types[pidx];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, NULL);

    for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
      GPR_ASSERT(grpc_cq_begin_op(cc, tags[i]));
      grpc_cq_end_op(&exec_ctx, cc, tags[i], GRPC_ERROR_NONE,
                     do_nothing_end_completion, NULL, &completions[i]);
    }

    for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
      ev = grpc_completion_queue_pluck(cc, tags[i],
                                       gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
      GPR_ASSERT(ev.tag == tags[i]);
    }

    for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
      GPR_ASSERT(grpc_cq_begin_op(cc, tags[i]));
      grpc_cq_end_op(&exec_ctx, cc, tags[i], GRPC_ERROR_NONE,
                     do_nothing_end_completion, NULL, &completions[i]);
    }

    for (i = 0; i < GPR_ARRAY_SIZE(tags); i++) {
      ev = grpc_completion_queue_pluck(cc, tags[GPR_ARRAY_SIZE(tags) - i - 1],
                                       gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
      GPR_ASSERT(ev.tag == tags[GPR_ARRAY_SIZE(tags) - i - 1]);
    }

    shutdown_and_destroy(cc);
    grpc_exec_ctx_finish(&exec_ctx);
  }
}

static void test_pluck_after_shutdown(void) {
  grpc_cq_polling_type polling_types[] = {
      GRPC_CQ_DEFAULT_POLLING, GRPC_CQ_NON_LISTENING, GRPC_CQ_NON_POLLING};
  grpc_event ev;
  grpc_completion_queue *cc;
  grpc_completion_queue_attributes attr;

  LOG_TEST("test_pluck_after_shutdown");

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_PLUCK;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(polling_types); i++) {
    attr.cq_polling_type = polling_types[i];
    cc = grpc_completion_queue_create(
        grpc_completion_queue_factory_lookup(&attr), &attr, NULL);
    grpc_completion_queue_shutdown(cc);
    ev = grpc_completion_queue_pluck(cc, NULL,
                                     gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    GPR_ASSERT(ev.type == GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cc);
  }
}

struct thread_state {
  grpc_completion_queue *cc;
  void *tag;
};

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_no_op();
  test_pollset_conversion();
  test_wait_empty();
  test_shutdown_then_next_polling();
  test_shutdown_then_next_with_timeout();
  test_cq_end_op();
  test_pluck();
  test_pluck_after_shutdown();
  grpc_shutdown();
  return 0;
}
