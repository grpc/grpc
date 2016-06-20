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

#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"

/*******************************************************************************
 * NOTE: If this test fails to compile, then the api changes are likely to cause
 *       merge failures downstream. Please pay special attention to reviewing
 *       these changes, and solicit help as appropriate when merging downstream.
 *
 * This test is NOT expected to be run directly.
 ******************************************************************************/

static void test_code(void) {
  /* iomgr.h */
  grpc_iomgr_init();
  grpc_iomgr_shutdown();

  /* closure.h */
  grpc_closure closure;
  closure.cb = NULL;
  closure.cb_arg = NULL;
  closure.next_data.scratch = 0;

  grpc_closure_list closure_list = GRPC_CLOSURE_LIST_INIT;
  closure_list.head = NULL;
  closure_list.tail = NULL;

  grpc_closure_init(&closure, NULL, NULL);

  grpc_closure_create(NULL, NULL);

  grpc_closure_list_move(NULL, NULL);
  grpc_closure_list_append(NULL, NULL, GRPC_ERROR_CREATE("Foo"));
  grpc_closure_list_empty(closure_list);

  /* exec_ctx.h */
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_exec_ctx_flush(&exec_ctx);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_exec_ctx_sched(&exec_ctx, &closure, GRPC_ERROR_CREATE("Foo"), NULL);
  grpc_exec_ctx_enqueue_list(&exec_ctx, &closure_list, NULL);

  /* endpoint.h */
  grpc_endpoint endpoint;
  grpc_endpoint_vtable vtable = {
      grpc_endpoint_read,           grpc_endpoint_write,
      grpc_endpoint_add_to_pollset, grpc_endpoint_add_to_pollset_set,
      grpc_endpoint_shutdown,       grpc_endpoint_destroy,
      grpc_endpoint_get_peer};
  endpoint.vtable = &vtable;

  grpc_endpoint_read(&exec_ctx, &endpoint, NULL, NULL);
  grpc_endpoint_get_peer(&endpoint);
  grpc_endpoint_write(&exec_ctx, &endpoint, NULL, NULL);
  grpc_endpoint_shutdown(&exec_ctx, &endpoint);
  grpc_endpoint_destroy(&exec_ctx, &endpoint);
  grpc_endpoint_add_to_pollset(&exec_ctx, &endpoint, NULL);
  grpc_endpoint_add_to_pollset_set(&exec_ctx, &endpoint, NULL);

  /* executor.h */
  grpc_executor_init();
  grpc_executor_push(&closure, GRPC_ERROR_CREATE("Phi"));
  grpc_executor_shutdown();

  /* pollset.h */
  grpc_pollset_size();
  grpc_pollset_init(NULL, NULL);
  grpc_pollset_shutdown(NULL, NULL, NULL);
  grpc_pollset_reset(NULL);
  grpc_pollset_destroy(NULL);
  GRPC_ERROR_UNREF(grpc_pollset_work(NULL, NULL, NULL,
                                     gpr_now(GPR_CLOCK_REALTIME),
                                     gpr_now(GPR_CLOCK_MONOTONIC)));
  GRPC_ERROR_UNREF(grpc_pollset_kick(NULL, NULL));
}

int main(void) {
  if (false) test_code();
  return 0;
}
