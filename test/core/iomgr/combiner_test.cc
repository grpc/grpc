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

#include "src/core/lib/iomgr/combiner.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/thd.h"
#include "test/core/util/test_config.h"

static void test_no_op(void) {
  gpr_log(GPR_DEBUG, "test_no_op");
  grpc_core::ExecCtx exec_ctx;
  GRPC_COMBINER_UNREF(grpc_combiner_create(), "test_no_op");
}

static void set_event_to_true(void* value, grpc_error* error) {
  gpr_event_set(static_cast<gpr_event*>(value), (void*)1);
}

static void test_execute_one(void) {
  gpr_log(GPR_DEBUG, "test_execute_one");

  grpc_combiner* lock = grpc_combiner_create();
  gpr_event done;
  gpr_event_init(&done);
  grpc_core::ExecCtx exec_ctx;
  GRPC_CLOSURE_SCHED(GRPC_CLOSURE_CREATE(set_event_to_true, &done,
                                         grpc_combiner_scheduler(lock)),
                     GRPC_ERROR_NONE);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&done, grpc_timeout_seconds_to_deadline(5)) !=
             nullptr);
  GRPC_COMBINER_UNREF(lock, "test_execute_one");
}

typedef struct {
  size_t ctr;
  grpc_combiner* lock;
  gpr_event done;
} thd_args;

typedef struct {
  size_t* ctr;
  size_t value;
} ex_args;

static void check_one(void* a, grpc_error* error) {
  ex_args* args = static_cast<ex_args*>(a);
  GPR_ASSERT(*args->ctr == args->value - 1);
  *args->ctr = args->value;
  gpr_free(a);
}

static void execute_many_loop(void* a) {
  thd_args* args = static_cast<thd_args*>(a);
  grpc_core::ExecCtx exec_ctx;
  size_t n = 1;
  for (size_t i = 0; i < 10; i++) {
    for (size_t j = 0; j < 10000; j++) {
      ex_args* c = static_cast<ex_args*>(gpr_malloc(sizeof(*c)));
      c->ctr = &args->ctr;
      c->value = n++;
      GRPC_CLOSURE_SCHED(GRPC_CLOSURE_CREATE(
                             check_one, c, grpc_combiner_scheduler(args->lock)),
                         GRPC_ERROR_NONE);
      grpc_core::ExecCtx::Get()->Flush();
    }
    // sleep for a little bit, to test a combiner draining and another thread
    // picking it up
    gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  }
  GRPC_CLOSURE_SCHED(GRPC_CLOSURE_CREATE(set_event_to_true, &args->done,
                                         grpc_combiner_scheduler(args->lock)),
                     GRPC_ERROR_NONE);
}

static void test_execute_many(void) {
  gpr_log(GPR_DEBUG, "test_execute_many");

  grpc_combiner* lock = grpc_combiner_create();
  grpc_core::Thread thds[100];
  thd_args ta[GPR_ARRAY_SIZE(thds)];
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    ta[i].ctr = 0;
    ta[i].lock = lock;
    gpr_event_init(&ta[i].done);
    thds[i] = grpc_core::Thread("grpc_execute_many", execute_many_loop, &ta[i]);
    thds[i].Start();
  }
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    GPR_ASSERT(gpr_event_wait(&ta[i].done,
                              gpr_inf_future(GPR_CLOCK_REALTIME)) != nullptr);
    thds[i].Join();
  }
  grpc_core::ExecCtx exec_ctx;
  GRPC_COMBINER_UNREF(lock, "test_execute_many");
}

static gpr_event got_in_finally;

static void in_finally(void* arg, grpc_error* error) {
  gpr_event_set(&got_in_finally, (void*)1);
}

static void add_finally(void* arg, grpc_error* error) {
  GRPC_CLOSURE_SCHED(GRPC_CLOSURE_CREATE(in_finally, arg,
                                         grpc_combiner_finally_scheduler(
                                             static_cast<grpc_combiner*>(arg))),
                     GRPC_ERROR_NONE);
}

static void test_execute_finally(void) {
  gpr_log(GPR_DEBUG, "test_execute_finally");

  grpc_combiner* lock = grpc_combiner_create();
  grpc_core::ExecCtx exec_ctx;
  gpr_event_init(&got_in_finally);
  GRPC_CLOSURE_SCHED(
      GRPC_CLOSURE_CREATE(add_finally, lock, grpc_combiner_scheduler(lock)),
      GRPC_ERROR_NONE);
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(gpr_event_wait(&got_in_finally,
                            grpc_timeout_seconds_to_deadline(5)) != nullptr);
  GRPC_COMBINER_UNREF(lock, "test_execute_finally");
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_no_op();
  test_execute_one();
  test_execute_finally();
  test_execute_many();
  grpc_shutdown();

  return 0;
}
