/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/iomgr/resolve_address.h"

#include <string.h>
#include <sys/un.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/util/test_config.h"

static gpr_timespec test_deadline(void) {
  return grpc_timeout_seconds_to_deadline(100);
}

typedef struct args_struct {
  gpr_event ev;
  grpc_resolved_addresses *addrs;
  gpr_atm done_atm;
  gpr_mu *mu;
  grpc_pollset *pollset;
  grpc_pollset_set *pollset_set;
} args_struct;

static void do_nothing(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {}

void args_init(grpc_exec_ctx *exec_ctx, args_struct *args) {
  gpr_event_init(&args->ev);
  args->pollset = gpr_zalloc(grpc_pollset_size());
  grpc_pollset_init(args->pollset, &args->mu);
  args->pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(exec_ctx, args->pollset_set, args->pollset);
  args->addrs = NULL;
}

void args_finish(grpc_exec_ctx *exec_ctx, args_struct *args) {
  GPR_ASSERT(gpr_event_wait(&args->ev, test_deadline()));
  grpc_resolved_addresses_destroy(args->addrs);
  grpc_pollset_set_del_pollset(exec_ctx, args->pollset_set, args->pollset);
  grpc_pollset_set_destroy(exec_ctx, args->pollset_set);
  grpc_closure do_nothing_cb;
  GRPC_CLOSURE_INIT(&do_nothing_cb, do_nothing, NULL,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(exec_ctx, args->pollset, &do_nothing_cb);
  // exec_ctx needs to be flushed before calling grpc_pollset_destroy()
  grpc_exec_ctx_flush(exec_ctx);
  grpc_pollset_destroy(exec_ctx, args->pollset);
  gpr_free(args->pollset);
}

static gpr_timespec n_sec_deadline(int seconds) {
  return gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                      gpr_time_from_seconds(seconds, GPR_TIMESPAN));
}

static void actually_poll(void *argsp) {
  args_struct *args = argsp;
  gpr_timespec deadline = n_sec_deadline(10);
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
    grpc_pollset_worker *worker = NULL;
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

static void test_unix_socket(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  args_struct args;
  args_init(&exec_ctx, &args);
  poll_pollset_until_request_done(&args);
  grpc_resolve_address(
      &exec_ctx, "unix:/path/name", NULL, args.pollset_set,
      GRPC_CLOSURE_CREATE(must_succeed, &args, grpc_schedule_on_exec_ctx),
      &args.addrs);
  args_finish(&exec_ctx, &args);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_unix_socket_path_name_too_long(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  args_struct args;
  args_init(&exec_ctx, &args);
  const char prefix[] = "unix:/path/name";
  size_t path_name_length =
      GPR_ARRAY_SIZE(((struct sockaddr_un *)0)->sun_path) + 6;
  char *path_name = gpr_malloc(sizeof(char) * path_name_length);
  memset(path_name, 'a', path_name_length);
  memcpy(path_name, prefix, strlen(prefix) - 1);
  path_name[path_name_length - 1] = '\0';

  poll_pollset_until_request_done(&args);
  grpc_resolve_address(
      &exec_ctx, path_name, NULL, args.pollset_set,
      GRPC_CLOSURE_CREATE(must_fail, &args, grpc_schedule_on_exec_ctx),
      &args.addrs);
  gpr_free(path_name);
  args_finish(&exec_ctx, &args);
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_iomgr_init(&exec_ctx);
  grpc_iomgr_start(&exec_ctx);
  test_unix_socket();
  test_unix_socket_path_name_too_long();
  grpc_executor_shutdown(&exec_ctx);
  grpc_iomgr_shutdown(&exec_ctx);
  grpc_exec_ctx_finish(&exec_ctx);
  return 0;
}
