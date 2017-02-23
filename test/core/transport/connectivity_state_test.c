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
      grpc_closure_create(must_fail, THE_ARG, grpc_schedule_on_exec_ctx);
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
      grpc_closure_create(must_succeed, THE_ARG, grpc_schedule_on_exec_ctx);
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
      grpc_closure_create(must_fail, THE_ARG, grpc_schedule_on_exec_ctx);
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
  grpc_connectivity_state_trace = 1;
  test_connectivity_state_name();
  test_check();
  test_subscribe_then_unsubscribe();
  test_subscribe_then_destroy();
  test_subscribe_with_failure_then_destroy();
  return 0;
}
