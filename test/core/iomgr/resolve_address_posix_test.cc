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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/util/test_config.h"

static gpr_timespec test_deadline(void) {
  return grpc_timeout_seconds_to_deadline(100);
}

typedef struct args_struct {
  grpc_core::Thread thd;
  gpr_event ev;
  grpc_resolved_addresses* addrs;
  gpr_atm done_atm;
  gpr_mu* mu;
  grpc_pollset* pollset;
  grpc_pollset_set* pollset_set;
} args_struct;

static void do_nothing(void* arg, grpc_error* error) {}

void args_init(args_struct* args) {
  gpr_event_init(&args->ev);
  args->pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(args->pollset, &args->mu);
  args->pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(args->pollset_set, args->pollset);
  args->addrs = nullptr;
}

void args_finish(args_struct* args) {
  GPR_ASSERT(gpr_event_wait(&args->ev, test_deadline()));
  args->thd.Join();
  // Don't need to explicitly destruct args->thd since
  // args is actually going to be destructed, not just freed
  grpc_resolved_addresses_destroy(args->addrs);
  grpc_pollset_set_del_pollset(args->pollset_set, args->pollset);
  grpc_pollset_set_destroy(args->pollset_set);
  grpc_closure do_nothing_cb;
  GRPC_CLOSURE_INIT(&do_nothing_cb, do_nothing, nullptr,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(args->pollset, &do_nothing_cb);
  // exec_ctx needs to be flushed before calling grpc_pollset_destroy()
  grpc_core::ExecCtx::Get()->Flush();
  grpc_pollset_destroy(args->pollset);
  gpr_free(args->pollset);
}

static grpc_millis n_sec_deadline(int seconds) {
  return grpc_timespec_to_millis_round_up(
      grpc_timeout_seconds_to_deadline(seconds));
}

static void actually_poll(void* argsp) {
  args_struct* args = static_cast<args_struct*>(argsp);
  grpc_millis deadline = n_sec_deadline(10);
  while (true) {
    grpc_core::ExecCtx exec_ctx;
    bool done = gpr_atm_acq_load(&args->done_atm) != 0;
    if (done) {
      break;
    }
    grpc_millis time_left = deadline - grpc_core::ExecCtx::Get()->Now();
    gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRIdPTR, done, time_left);
    GPR_ASSERT(time_left >= 0);
    grpc_pollset_worker* worker = nullptr;
    gpr_mu_lock(args->mu);
    GRPC_LOG_IF_ERROR("pollset_work", grpc_pollset_work(args->pollset, &worker,
                                                        n_sec_deadline(1)));
    gpr_mu_unlock(args->mu);
    grpc_core::ExecCtx::Get()->Flush();
  }
  gpr_event_set(&args->ev, (void*)1);
}

static void poll_pollset_until_request_done(args_struct* args) {
  gpr_atm_rel_store(&args->done_atm, 0);
  args->thd = grpc_core::Thread("grpc_poll_pollset", actually_poll, args);
  args->thd.Start();
}

static void must_succeed(void* argsp, grpc_error* err) {
  args_struct* args = static_cast<args_struct*>(argsp);
  GPR_ASSERT(err == GRPC_ERROR_NONE);
  GPR_ASSERT(args->addrs != nullptr);
  GPR_ASSERT(args->addrs->naddrs > 0);
  gpr_atm_rel_store(&args->done_atm, 1);
}

static void must_fail(void* argsp, grpc_error* err) {
  args_struct* args = static_cast<args_struct*>(argsp);
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  gpr_atm_rel_store(&args->done_atm, 1);
}

static void test_unix_socket(void) {
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  poll_pollset_until_request_done(&args);
  grpc_resolve_address(
      "unix:/path/name", nullptr, args.pollset_set,
      GRPC_CLOSURE_CREATE(must_succeed, &args, grpc_schedule_on_exec_ctx),
      &args.addrs);
  args_finish(&args);
}

static void test_unix_socket_path_name_too_long(void) {
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  const char prefix[] = "unix:/path/name";
  size_t path_name_length =
      GPR_ARRAY_SIZE(((struct sockaddr_un*)nullptr)->sun_path) + 6;
  char* path_name =
      static_cast<char*>(gpr_malloc(sizeof(char) * path_name_length));
  memset(path_name, 'a', path_name_length);
  memcpy(path_name, prefix, strlen(prefix) - 1);
  path_name[path_name_length - 1] = '\0';

  poll_pollset_until_request_done(&args);
  grpc_resolve_address(
      path_name, nullptr, args.pollset_set,
      GRPC_CLOSURE_CREATE(must_fail, &args, grpc_schedule_on_exec_ctx),
      &args.addrs);
  gpr_free(path_name);
  args_finish(&args);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  {
    grpc_core::ExecCtx exec_ctx;
    test_unix_socket();
    test_unix_socket_path_name_too_long();
  }

  grpc_shutdown();
  return 0;
}
