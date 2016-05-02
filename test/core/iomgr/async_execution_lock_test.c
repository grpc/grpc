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

static void test_no_op(void) {
  gpr_log(GPR_DEBUG, "test_no_op");

  grpc_aelock lock;
  grpc_aelock_init(&lock, NULL);
  grpc_aelock_destroy(&lock);
}

static void set_bool_to_true(grpc_exec_ctx *exec_ctx, void *value) {
  *(bool *)value = true;
}

static void test_execute_one(void) {
  gpr_log(GPR_DEBUG, "test_execute_one");

  grpc_aelock lock;
  grpc_aelock_init(&lock, NULL);
  bool done = false;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_aelock_execute(&exec_ctx, &lock, set_bool_to_true, &done, 0);
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(done);
  grpc_aelock_destroy(&lock);
}

typedef struct {
  size_t *ctr;
  size_t value;
} ex_args;

static void check_one(grpc_exec_ctx *exec_ctx, void *a) {
  ex_args *args = a;
  GPR_ASSERT(*args->ctr == args->value - 1);
  *args->ctr = args->value;
}

static void execute_many_loop(void *lock) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  for (size_t i = 0; i < 100; i++) {
    size_t ctr = 0;
    for (size_t j = 1; j <= 1000; j++) {
      ex_args args = {&ctr, j};
      grpc_aelock_execute(&exec_ctx, lock, check_one, &args, sizeof(args));
      grpc_exec_ctx_flush(&exec_ctx);
    }
    gpr_sleep_until(GRPC_TIMEOUT_MILLIS_TO_DEADLINE(1));
  }
}

static void test_execute_many(void) {
  grpc_aelock lock;
  gpr_thd_id thds[100];
  grpc_aelock_init(&lock, NULL);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    gpr_thd_options options = gpr_thd_options_default();
    gpr_thd_options_set_joinable(&options);
    GPR_ASSERT(gpr_thd_new(&thds[i], execute_many_loop, &lock, &options));
  }
  for (size_t i = 0; i < GPR_ARRAY_SIZE(thds); i++) {
    gpr_thd_join(thds[i]);
  }
  grpc_aelock_destroy(&lock);
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
