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

#include "src/core/lib/transport/connectivity_state.h"

#include <string.h>

#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

#define THE_ARG ((void *)(size_t)0xcafebabe)

int g_counter;

static void must_succeed(grpc_exec_ctx *exec_ctx, void *arg,
                         grpc_error *error) {
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(arg == THE_ARG);
  g_counter++;
}

static void must_fail(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  GPR_ASSERT(arg == THE_ARG);
  g_counter++;
}

static void test_connectivity_state_name(void) {
  gpr_log(GPR_DEBUG, "test_connectivity_state_name");
  GPR_ASSERT(0 ==
             strcmp(grpc_connectivity_state_name(GRPC_CHANNEL_IDLE), "IDLE"));
  GPR_ASSERT(0 == strcmp(grpc_connectivity_state_name(GRPC_CHANNEL_CONNECTING),
                         "CONNECTING"));
  GPR_ASSERT(0 ==
             strcmp(grpc_connectivity_state_name(GRPC_CHANNEL_READY), "READY"));
  GPR_ASSERT(
      0 == strcmp(grpc_connectivity_state_name(GRPC_CHANNEL_TRANSIENT_FAILURE),
                  "TRANSIENT_FAILURE"));
  GPR_ASSERT(0 == strcmp(grpc_connectivity_state_name(GRPC_CHANNEL_SHUTDOWN),
                         "SHUTDOWN"));
}

static void test_check(void) {
  grpc_connectivity_state_tracker tracker;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_error *error;
  gpr_log(GPR_DEBUG, "test_check");
  grpc_connectivity_state_init(&tracker, GRPC_CHANNEL_IDLE, "xxx");
  GPR_ASSERT(grpc_connectivity_state_get(&tracker, &error) ==
             GRPC_CHANNEL_IDLE);
  GPR_ASSERT(grpc_connectivity_state_check(&tracker) == GRPC_CHANNEL_IDLE);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_connectivity_state_destroy(&exec_ctx, &tracker);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_subscribe_then_unsubscribe(void) {
  grpc_connectivity_state_tracker tracker;
  grpc_closure *closure =
      GRPC_CLOSURE_CREATE(must_fail, THE_ARG, grpc_schedule_on_exec_ctx);
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_log(GPR_DEBUG, "test_subscribe_then_unsubscribe");
  g_counter = 0;
  grpc_connectivity_state_init(&tracker, GRPC_CHANNEL_IDLE, "xxx");
  GPR_ASSERT(grpc_connectivity_state_notify_on_state_change(&exec_ctx, &tracker,
                                                            &state, closure));
  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(state == GRPC_CHANNEL_IDLE);
  GPR_ASSERT(g_counter == 0);
  grpc_connectivity_state_notify_on_state_change(&exec_ctx, &tracker, NULL,
                                                 closure);
  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(state == GRPC_CHANNEL_IDLE);
  GPR_ASSERT(g_counter == 1);

  grpc_connectivity_state_destroy(&exec_ctx, &tracker);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_subscribe_then_destroy(void) {
  grpc_connectivity_state_tracker tracker;
  grpc_closure *closure =
      GRPC_CLOSURE_CREATE(must_succeed, THE_ARG, grpc_schedule_on_exec_ctx);
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_log(GPR_DEBUG, "test_subscribe_then_destroy");
  g_counter = 0;
  grpc_connectivity_state_init(&tracker, GRPC_CHANNEL_IDLE, "xxx");
  GPR_ASSERT(grpc_connectivity_state_notify_on_state_change(&exec_ctx, &tracker,
                                                            &state, closure));
  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(state == GRPC_CHANNEL_IDLE);
  GPR_ASSERT(g_counter == 0);
  grpc_connectivity_state_destroy(&exec_ctx, &tracker);
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(state == GRPC_CHANNEL_SHUTDOWN);
  GPR_ASSERT(g_counter == 1);
}

static void test_subscribe_with_failure_then_destroy(void) {
  grpc_connectivity_state_tracker tracker;
  grpc_closure *closure =
      GRPC_CLOSURE_CREATE(must_fail, THE_ARG, grpc_schedule_on_exec_ctx);
  grpc_connectivity_state state = GRPC_CHANNEL_SHUTDOWN;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_log(GPR_DEBUG, "test_subscribe_with_failure_then_destroy");
  g_counter = 0;
  grpc_connectivity_state_init(&tracker, GRPC_CHANNEL_SHUTDOWN, "xxx");
  GPR_ASSERT(0 == grpc_connectivity_state_notify_on_state_change(
                      &exec_ctx, &tracker, &state, closure));
  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(state == GRPC_CHANNEL_SHUTDOWN);
  GPR_ASSERT(g_counter == 0);
  grpc_connectivity_state_destroy(&exec_ctx, &tracker);
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(state == GRPC_CHANNEL_SHUTDOWN);
  GPR_ASSERT(g_counter == 1);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_connectivity_state_trace.value = 1;
  test_connectivity_state_name();
  test_check();
  test_subscribe_then_unsubscribe();
  test_subscribe_then_destroy();
  test_subscribe_with_failure_then_destroy();
  return 0;
}
