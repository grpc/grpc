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

#include "src/core/iomgr/workqueue.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

static grpc_pollset g_pollset;

static void must_succeed(void *p, int success, grpc_call_list *call_list) {
  GPR_ASSERT(success == 1);
  gpr_mu_lock(GRPC_POLLSET_MU(&g_pollset));
  *(int *)p = 1;
  grpc_pollset_kick(&g_pollset, NULL);
  gpr_mu_unlock(GRPC_POLLSET_MU(&g_pollset));
}

static void test_add_closure(void) {
  grpc_closure c;
  int done = 0;
  grpc_call_list call_list = GRPC_CALL_LIST_INIT;
  grpc_workqueue *wq = grpc_workqueue_create(&call_list);
  gpr_timespec deadline = GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5);
  grpc_pollset_worker worker;
  grpc_closure_init(&c, must_succeed, &done);

  grpc_workqueue_push(wq, &c, 1);
  grpc_workqueue_add_to_pollset(wq, &g_pollset, &call_list);

  gpr_mu_lock(GRPC_POLLSET_MU(&g_pollset));
  GPR_ASSERT(!done);
  grpc_pollset_work(&g_pollset, &worker, gpr_now(deadline.clock_type), deadline,
                    &call_list);
  gpr_mu_unlock(GRPC_POLLSET_MU(&g_pollset));
  grpc_call_list_run(&call_list);
  GPR_ASSERT(done);

  GRPC_WORKQUEUE_UNREF(wq, "destroy", &call_list);
  grpc_call_list_run(&call_list);
}

static void destroy_pollset(void *p, int success, grpc_call_list *call_list) {
  grpc_pollset_destroy(p);
}

int main(int argc, char **argv) {
  grpc_closure destroyed;
  grpc_call_list call_list = GRPC_CALL_LIST_INIT;
  grpc_test_init(argc, argv);
  grpc_init();
  grpc_pollset_init(&g_pollset);

  test_add_closure();

  grpc_closure_init(&destroyed, destroy_pollset, &g_pollset);
  grpc_pollset_shutdown(&g_pollset, &destroyed, &call_list);
  grpc_call_list_run(&call_list);
  grpc_shutdown();
  return 0;
}
