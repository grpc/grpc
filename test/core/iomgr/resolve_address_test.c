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

#include "src/core/lib/iomgr/resolve_address.h"
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/util/test_config.h"

static gpr_timespec test_deadline(void) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(100);
}

typedef struct args_struct {
  gpr_event ev;
  grpc_resolved_addresses *addrs;
  gpr_atm done_atm;
  gpr_mu *mu;
  grpc_pollset *pollset;
  grpc_pollset_set *pollset_set;
} args_struct;

void args_init(args_struct *args) {
  gpr_event_init(&args->ev);
  grpc_pollset_init(args->pollset, &args->mu);
  args->pollset_set = grpc_pollset_set_create();
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_pollset_set_add_pollset(&exec_ctx, args->pollset_set, args->pollset);
  grpc_exec_ctx_finish(&exec_ctx);
  args->addrs = NULL;
}

void args_finish(args_struct *args) {
  GPR_ASSERT(gpr_event_wait(&args->ev, test_deadline()));
  grpc_resolved_addresses_destroy(args->addrs);
  grpc_pollset_set_destroy(args->pollset_set);
  grpc_pollset_destroy(args->pollset);
}

static gpr_timespec n_sec_deadline(int seconds) {
  return gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                      gpr_time_from_seconds(seconds, GPR_TIMESPAN));
}

static void actually_poll(void *argsp) {
  args_struct *args = argsp;
  gpr_timespec deadline = n_sec_deadline(10);
  grpc_pollset_worker *worker = NULL;
  while (true) {
    bool done = gpr_atm_acq_load(&args->done_atm) != 0;
    if (done) {
      break;
    }
    gpr_timespec time_left =
        gpr_time_sub(deadline, gpr_now(GPR_CLOCK_REALTIME));
    gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRId64 ".%09d", done,
            time_left.tv_sec, time_left.tv_nsec);
    GPR_ASSERT(gpr_time_cmp(time_left, gpr_time_0(GPR_TIMESPAN)) >= 0);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    gpr_mu_lock(args->mu);
    GRPC_LOG_IF_ERROR(
        "pollset_work",
        grpc_pollset_work(&exec_ctx, args->pollset, &worker,
                          gpr_now(GPR_CLOCK_REALTIME), n_sec_deadline(1)));
    gpr_mu_unlock(args->mu);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  gpr_event_set(&args->ev, (void *)1);
}

static void poll_pollset_until_request_done(args_struct *args) {
  gpr_atm_rel_store(&args->done_atm, 0);
  gpr_thd_id id;
  gpr_thd_new(&id, actually_poll, args, NULL);
}

static void must_succeed(grpc_exec_ctx *exec_ctx, void *argsp,
                         grpc_error *err) {
  args_struct *args = argsp;
  GPR_ASSERT(err == GRPC_ERROR_NONE);
  GPR_ASSERT(args->addrs != NULL);
  GPR_ASSERT(args->addrs->naddrs > 0);
  gpr_atm_rel_store(&args->done_atm, 1);
}

static void must_fail(grpc_exec_ctx *exec_ctx, void *argsp, grpc_error *err) {
  args_struct *args = argsp;
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  gpr_atm_rel_store(&args->done_atm, 1);
}

static void test_localhost(void) {
  args_struct args;
  args_init(&args);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  poll_pollset_until_request_done(&args);
  grpc_resolve_address(&exec_ctx, "localhost:1", NULL, args.pollset_set,
                       grpc_closure_create(must_succeed, &args), &args.addrs);
  grpc_exec_ctx_finish(&exec_ctx);
  args_finish(&args);
}

static void test_default_port(void) {
  args_struct args;
  args_init(&args);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  poll_pollset_until_request_done(&args);
  grpc_resolve_address(&exec_ctx, "localhost", "1", args.pollset_set,
                       grpc_closure_create(must_succeed, &args), &args.addrs);
  grpc_exec_ctx_finish(&exec_ctx);
  args_finish(&args);
}

static void test_missing_default_port(void) {
  args_struct args;
  args_init(&args);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  poll_pollset_until_request_done(&args);
  grpc_resolve_address(&exec_ctx, "localhost", NULL, args.pollset_set,
                       grpc_closure_create(must_fail, &args), &args.addrs);
  grpc_exec_ctx_finish(&exec_ctx);
  args_finish(&args);
}

static void test_ipv6_with_port(void) {
  args_struct args;
  args_init(&args);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  poll_pollset_until_request_done(&args);
  grpc_resolve_address(&exec_ctx, "[2001:db8::1]:1", NULL, args.pollset_set,
                       grpc_closure_create(must_succeed, &args), &args.addrs);
  grpc_exec_ctx_finish(&exec_ctx);
  args_finish(&args);
}

static void test_ipv6_without_port(void) {
  const char *const kCases[] = {
      "2001:db8::1", "2001:db8::1.2.3.4", "[2001:db8::1]",
  };
  unsigned i;
  for (i = 0; i < sizeof(kCases) / sizeof(*kCases); i++) {
    args_struct args;
    args_init(&args);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    poll_pollset_until_request_done(&args);
    grpc_resolve_address(&exec_ctx, kCases[i], "80", args.pollset_set,
                         grpc_closure_create(must_succeed, &args), &args.addrs);
    grpc_exec_ctx_finish(&exec_ctx);
    args_finish(&args);
  }
}

static void test_invalid_ip_addresses(void) {
  const char *const kCases[] = {
      "293.283.1238.3:1", "[2001:db8::11111]:1",
  };
  unsigned i;
  for (i = 0; i < sizeof(kCases) / sizeof(*kCases); i++) {
    args_struct args;
    args_init(&args);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    poll_pollset_until_request_done(&args);
    grpc_resolve_address(&exec_ctx, kCases[i], NULL, args.pollset_set,
                         grpc_closure_create(must_fail, &args), &args.addrs);
    grpc_exec_ctx_finish(&exec_ctx);
    args_finish(&args);
  }
}

static void test_unparseable_hostports(void) {
  const char *const kCases[] = {
      "[", "[::1", "[::1]bad", "[1.2.3.4]", "[localhost]", "[localhost]:1",
  };
  unsigned i;
  for (i = 0; i < sizeof(kCases) / sizeof(*kCases); i++) {
    args_struct args;
    args_init(&args);
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    poll_pollset_until_request_done(&args);
    grpc_resolve_address(&exec_ctx, kCases[i], "1", args.pollset_set,
                         grpc_closure_create(must_fail, &args), &args.addrs);
    grpc_exec_ctx_finish(&exec_ctx);
    args_finish(&args);
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_executor_init();
  grpc_iomgr_init();
  test_localhost();
  test_default_port();
  test_missing_default_port();
  test_ipv6_with_port();
  test_ipv6_without_port();
  test_invalid_ip_addresses();
  test_unparseable_hostports();
  grpc_iomgr_shutdown();
  grpc_executor_shutdown();
  return 0;
}
