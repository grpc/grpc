/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/iomgr/workqueue.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

static void must_succeed(grpc_exec_ctx *exec_ctx, void *p, bool success) {
  GPR_ASSERT(success);
  ++*(int *)p;
}

static void test_enqueue(void) {
  gpr_log(GPR_INFO, "test_enqueue");

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  int n = 0;
  grpc_exec_ctx_enqueue(&exec_ctx, grpc_closure_create(must_succeed, &n), true,
                        NULL);
  GPR_ASSERT(n == 0);

  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(n == 1);
}

static void test_enqueue_with_flush(void) {
  gpr_log(GPR_INFO, "test_enqueue_with_flush");

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  int n = 0;
  grpc_exec_ctx_enqueue(&exec_ctx, grpc_closure_create(must_succeed, &n), true,
                        NULL);
  GPR_ASSERT(n == 0);

  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(n == 1);

  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(n == 1);
}

static void test_enqueue_with_offload_1(void) {
  gpr_log(GPR_INFO, "test_enqueue_with_offload_1");

  grpc_exec_ctx exec_ctx =
      GRPC_EXEC_CTX_INIT_WITH_OFFLOAD_CHECK(grpc_never_offload, NULL);
  grpc_workqueue *wq = grpc_workqueue_create(&exec_ctx);

  int n = 0;
  grpc_exec_ctx_enqueue(&exec_ctx, grpc_closure_create(must_succeed, &n), true,
                        wq);
  GPR_ASSERT(n == 0);

  GRPC_WORKQUEUE_UNREF(&exec_ctx, wq, "own");

  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(n == 1);
}

static void test_enqueue_with_offload_2(void) {
  gpr_log(GPR_INFO, "test_enqueue_with_offload_2");

  grpc_exec_ctx exec_ctx =
      GRPC_EXEC_CTX_INIT_WITH_OFFLOAD_CHECK(grpc_never_offload, NULL);
  grpc_workqueue *wq = grpc_workqueue_create(&exec_ctx);

  int n = 0;
  grpc_exec_ctx_enqueue(&exec_ctx, grpc_closure_create(must_succeed, &n), true,
                        wq);
  GPR_ASSERT(n == 0);

  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(n == 1);

  GRPC_WORKQUEUE_UNREF(&exec_ctx, wq, "own");

  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(n == 1);
}

static void test_enqueue_with_offload_3(void) {
  gpr_log(GPR_INFO, "test_enqueue_with_offload_3");

  grpc_exec_ctx exec_ctx =
      GRPC_EXEC_CTX_INIT_WITH_OFFLOAD_CHECK(grpc_never_offload, NULL);
  grpc_workqueue *wq = grpc_workqueue_create(&exec_ctx);

  int n = 0;
  grpc_exec_ctx_enqueue(&exec_ctx, grpc_closure_create(must_succeed, &n), true,
                        wq);
  GPR_ASSERT(n == 0);

  GRPC_WORKQUEUE_UNREF(&exec_ctx, wq, "own");

  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(n == 1);

  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(n == 1);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  test_enqueue();
  test_enqueue_with_flush();
  test_enqueue_with_offload_1();
  test_enqueue_with_offload_2();
  test_enqueue_with_offload_3();

  grpc_shutdown();
  return 0;
}
