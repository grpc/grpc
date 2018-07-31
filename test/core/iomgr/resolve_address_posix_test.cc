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

#include <net/if.h>
#include <string.h>
#include <sys/un.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/util/cmdline.h"
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
    gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRId64, done, time_left);
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
  gpr_mu_lock(args->mu);
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
  gpr_mu_unlock(args->mu);
}

static void must_fail(void* argsp, grpc_error* err) {
  args_struct* args = static_cast<args_struct*>(argsp);
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  gpr_atm_rel_store(&args->done_atm, 1);
  gpr_mu_lock(args->mu);
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
  gpr_mu_unlock(args->mu);
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

static void resolve_address_must_succeed(const char* target) {
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  poll_pollset_until_request_done(&args);
  grpc_resolve_address(
      target, "1" /* port number */, args.pollset_set,
      GRPC_CLOSURE_CREATE(must_succeed, &args, grpc_schedule_on_exec_ctx),
      &args.addrs);
  grpc_core::ExecCtx::Get()->Flush();
  args_finish(&args);
}

static void test_named_and_numeric_scope_ids(void) {
  char* arbitrary_interface_name = static_cast<char*>(gpr_zalloc(IF_NAMESIZE));
  int interface_index = 0;
  // Probe candidate interface index numbers until we find one that the
  // system recognizes, and then use that for the test.
  for (size_t i = 1; i < 65536; i++) {
    if (if_indextoname(i, arbitrary_interface_name) != nullptr) {
      gpr_log(
          GPR_DEBUG,
          "Found interface at index %d named %s. Will use this for the test",
          (int)i, arbitrary_interface_name);
      interface_index = (int)i;
      break;
    }
  }
  GPR_ASSERT(strlen(arbitrary_interface_name) > 0);
  // Test resolution of an ipv6 address with a named scope ID
  gpr_log(GPR_DEBUG, "test resolution with a named scope ID");
  char* target_with_named_scope_id = nullptr;
  gpr_asprintf(&target_with_named_scope_id, "fe80::1234%%%s",
               arbitrary_interface_name);
  resolve_address_must_succeed(target_with_named_scope_id);
  gpr_free(target_with_named_scope_id);
  gpr_free(arbitrary_interface_name);
  // Test resolution of an ipv6 address with a numeric scope ID
  gpr_log(GPR_DEBUG, "test resolution with a numeric scope ID");
  char* target_with_numeric_scope_id = nullptr;
  gpr_asprintf(&target_with_numeric_scope_id, "fe80::1234%%%d",
               interface_index);
  resolve_address_must_succeed(target_with_numeric_scope_id);
  gpr_free(target_with_numeric_scope_id);
}

int main(int argc, char** argv) {
  // First set the resolver type based off of --resolver
  const char* resolver_type = nullptr;
  gpr_cmdline* cl = gpr_cmdline_create("resolve address test");
  gpr_cmdline_add_string(cl, "resolver", "Resolver type (ares or native)",
                         &resolver_type);
  // In case that there are more than one argument on the command line,
  // --resolver will always be the first one, so only parse the first argument
  // (other arguments may be unknown to cl)
  gpr_cmdline_parse(cl, argc > 2 ? 2 : argc, argv);
  const char* cur_resolver = gpr_getenv("GRPC_DNS_RESOLVER");
  if (cur_resolver != nullptr && strlen(cur_resolver) != 0) {
    gpr_log(GPR_INFO, "Warning: overriding resolver setting of %s",
            cur_resolver);
  }
  if (gpr_stricmp(resolver_type, "native") == 0) {
    gpr_setenv("GRPC_DNS_RESOLVER", "native");
  } else if (gpr_stricmp(resolver_type, "ares") == 0) {
#ifndef GRPC_UV
    gpr_setenv("GRPC_DNS_RESOLVER", "ares");
#endif
  } else {
    gpr_log(GPR_ERROR, "--resolver_type was not set to ares or native");
    abort();
  }
  grpc_test_init(argc, argv);
  grpc_init();

  {
    grpc_core::ExecCtx exec_ctx;
    test_named_and_numeric_scope_ids();
    // c-ares doesn't implement the same UDS resolution that the native resolver
    // does (UDS resolution within native DNS resolution only intended for
    // server usage, which c-ares doesn't support).
    if (gpr_stricmp(resolver_type, "ares") != 0) {
      test_unix_socket();
      test_unix_socket_path_name_too_long();
    }
  }
  gpr_cmdline_destroy(cl);

  grpc_shutdown();
  return 0;
}
