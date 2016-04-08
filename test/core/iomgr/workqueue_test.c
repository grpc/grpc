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

#include "src/core/lib/iomgr/workqueue.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

static gpr_mu *g_mu;
static grpc_pollset *g_pollset;

static void must_succeed(grpc_exec_ctx *exec_ctx, void *p, bool success) {
  GPR_ASSERT(success == 1);
  gpr_mu_lock(g_mu);
  *(int *)p = 1;
  grpc_pollset_kick(g_pollset, NULL);
  gpr_mu_unlock(g_mu);
}

static void test_ref_unref(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_workqueue *wq = grpc_workqueue_create(&exec_ctx);
  GRPC_WORKQUEUE_REF(wq, "test");
  GRPC_WORKQUEUE_UNREF(&exec_ctx, wq, "test");
  GRPC_WORKQUEUE_UNREF(&exec_ctx, wq, "destroy");
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_add_closure(void) {
  grpc_closure c;
  int done = 0;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_workqueue *wq = grpc_workqueue_create(&exec_ctx);
  gpr_timespec deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5);
  grpc_pollset_worker *worker = NULL;
  grpc_closure_init(&c, must_succeed, &done);

  grpc_workqueue_push(wq, &c, 1);
  grpc_workqueue_add_to_pollset(&exec_ctx, wq, g_pollset);

  gpr_mu_lock(g_mu);
  GPR_ASSERT(!done);
  grpc_pollset_work(&exec_ctx, g_pollset, &worker, gpr_now(deadline.clock_type),
                    deadline);
  gpr_mu_unlock(g_mu);
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(done);

  GRPC_WORKQUEUE_UNREF(&exec_ctx, wq, "destroy");
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_flush(void) {
  grpc_closure c;
  int done = 0;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_workqueue *wq = grpc_workqueue_create(&exec_ctx);
  gpr_timespec deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5);
  grpc_pollset_worker *worker = NULL;
  grpc_closure_init(&c, must_succeed, &done);

  grpc_exec_ctx_enqueue(&exec_ctx, &c, true, NULL);
  grpc_workqueue_flush(&exec_ctx, wq);
  grpc_workqueue_add_to_pollset(&exec_ctx, wq, g_pollset);

  gpr_mu_lock(g_mu);
  GPR_ASSERT(!done);
  grpc_pollset_work(&exec_ctx, g_pollset, &worker, gpr_now(deadline.clock_type),
                    deadline);
  gpr_mu_unlock(g_mu);
  grpc_exec_ctx_finish(&exec_ctx);
  GPR_ASSERT(done);

  GRPC_WORKQUEUE_UNREF(&exec_ctx, wq, "destroy");
  grpc_exec_ctx_finish(&exec_ctx);
}

static void destroy_pollset(grpc_exec_ctx *exec_ctx, void *p, bool success) {
  grpc_pollset_destroy(p);
}

int main(int argc, char **argv) {
  grpc_closure destroyed;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_test_init(argc, argv);
  grpc_init();
  g_pollset = gpr_malloc(grpc_pollset_size());
  grpc_pollset_init(g_pollset, &g_mu);

  test_ref_unref();
  test_add_closure();
  test_flush();

  grpc_closure_init(&destroyed, destroy_pollset, g_pollset);
  grpc_pollset_shutdown(&exec_ctx, g_pollset, &destroyed);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_shutdown();

  gpr_free(g_pollset);
  return 0;
}
