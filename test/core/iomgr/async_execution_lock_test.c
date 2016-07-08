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

#include "src/core/lib/iomgr/async_execution_lock.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>

#include "test/core/util/test_config.h"

static void do_nothing_action(grpc_exec_ctx *exec_ctx, void *ignored) {}

static void test_no_op(void) {
  gpr_log(GPR_DEBUG, "test_no_op");
  grpc_aelock_destroy(grpc_aelock_create(NULL, do_nothing_action, NULL));
}

static void set_bool_to_true(grpc_exec_ctx *exec_ctx, void *value) {
  *(bool *)value = true;
}

static void increment_atomic(grpc_exec_ctx *exec_ctx, void *value) {
  gpr_atm_full_fetch_add((gpr_atm *)value, 1);
}

static void test_execute_one(void) {
  gpr_log(GPR_DEBUG, "test_execute_one");

  gpr_atm idles;
  gpr_atm_no_barrier_store(&idles, 0);
  grpc_aelock *lock = grpc_aelock_create(NULL, increment_atomic, &idles);
  bool done = false;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_aelock_execute(&exec_ctx, lock, set_bool_to_true, &done, 0);
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(done);
  grpc_aelock_destroy(lock);
  GPR_ASSERT(gpr_atm_no_barrier_load(&idles) == 1);
}

typedef struct {
  size_t ctr;
  grpc_aelock *lock;
} thd_args;

typedef struct {
  size_t *ctr;
  size_t value;
} ex_args;

static void check_one(grpc_exec_ctx *exec_ctx, void *a) {
  ex_args *args = a;
  // gpr_log(GPR_DEBUG, "*%p=%d; step %d", args->ctr, *args->ctr, args->value);
  GPR_ASSERT(*args->ctr == args->value - 1);
  *args->ctr = args->value;
}

static void execute_many_loop(void *a) {
  thd_args *args = a;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  size_t n = 1;
  for (size_t i = 0; i < 10; i++) {
    for (size_t j = 0; j < 100; j++) {
      ex_args c = {&args->ctr, n++};
      grpc_aelock_execute(&exec_ctx, args->lock, check_one, &c, sizeof(c));
      grpc_exec_ctx_flush(&exec_ctx);
    }
    gpr_sleep_until(GRPC_TIMEOUT_MILLIS_TO_DEADLINE(100));
  }
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_execute_many(void) {
  gpr_log(GPR_DEBUG, "test_execute_many");

  gpr_atm idles;
  gpr_atm_no_barrier_store(&idles, 0);

  grpc_aelock *lock = grpc_aelock_create(NULL, increment_atomic, &idles);
  gpr_thd_id thds[100];
  thd_args ta[GPR_ARRAY_SIZE(thds)];
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    gpr_thd_options options = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&options);
    ta[i].ctr = 0;
    ta[i].lock = lock;
    GPR_ASSERT(gpr_thd_new(&thds[i], execute_many_loop, &ta[i], &options));
  }
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    gpr_thd_join(thds[i]);
  }
  grpc_aelock_destroy(lock);

  gpr_log(GPR_DEBUG, "idles: %d", gpr_atm_no_barrier_load(&idles));
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_no_op();
  test_execute_one();
  test_execute_many();
  grpc_shutdown();

  return 0;
}
