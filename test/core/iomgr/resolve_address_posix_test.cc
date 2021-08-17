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

#include <string>

#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.h"
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
  gpr_mu* mu;
  bool done;              // guarded by mu
  grpc_pollset* pollset;  // guarded by mu
  grpc_pollset_set* pollset_set;
} args_struct;

static void do_nothing(void* /*arg*/, grpc_error_handle /*error*/) {}

void args_init(args_struct* args) {
  gpr_event_init(&args->ev);
  args->pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(args->pollset, &args->mu);
  args->pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(args->pollset_set, args->pollset);
  args->addrs = nullptr;
  args->done = false;
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
    {
      grpc_core::MutexLockForGprMu lock(args->mu);
      if (args->done) {
        break;
      }
      grpc_millis time_left = deadline - grpc_core::ExecCtx::Get()->Now();
      gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRId64, args->done, time_left);
      GPR_ASSERT(time_left >= 0);
      grpc_pollset_worker* worker = nullptr;
      GRPC_LOG_IF_ERROR(
          "pollset_work",
          grpc_pollset_work(args->pollset, &worker, n_sec_deadline(1)));
    }
  }
  gpr_event_set(&args->ev, reinterpret_cast<void*>(1));
}

static void poll_pollset_until_request_done(args_struct* args) {
  args->thd = grpc_core::Thread("grpc_poll_pollset", actually_poll, args);
  args->thd.Start();
}

static void must_succeed(void* argsp, grpc_error_handle err) {
  args_struct* args = static_cast<args_struct*>(argsp);
  GPR_ASSERT(err == GRPC_ERROR_NONE);
  GPR_ASSERT(args->addrs != nullptr);
  GPR_ASSERT(args->addrs->naddrs > 0);
  grpc_core::MutexLockForGprMu lock(args->mu);
  args->done = true;
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
}

static void must_fail(void* argsp, grpc_error_handle err) {
  args_struct* args = static_cast<args_struct*>(argsp);
  GPR_ASSERT(err != GRPC_ERROR_NONE);
  grpc_core::MutexLockForGprMu lock(args->mu);
  args->done = true;
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
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
      gpr_log(GPR_DEBUG,
              "Found interface at index %" PRIuPTR
              " named %s. Will use this for the test",
              i, arbitrary_interface_name);
      interface_index = static_cast<int>(i);
      break;
    }
  }
  GPR_ASSERT(strlen(arbitrary_interface_name) > 0);
  // Test resolution of an ipv6 address with a named scope ID
  gpr_log(GPR_DEBUG, "test resolution with a named scope ID");
  std::string target_with_named_scope_id =
      absl::StrFormat("fe80::1234%%%s", arbitrary_interface_name);
  resolve_address_must_succeed(target_with_named_scope_id.c_str());
  gpr_free(arbitrary_interface_name);
  // Test resolution of an ipv6 address with a numeric scope ID
  gpr_log(GPR_DEBUG, "test resolution with a numeric scope ID");
  std::string target_with_numeric_scope_id =
      absl::StrFormat("fe80::1234%%%d", interface_index);
  resolve_address_must_succeed(target_with_numeric_scope_id.c_str());
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
  grpc_core::UniquePtr<char> resolver =
      GPR_GLOBAL_CONFIG_GET(grpc_dns_resolver);
  if (strlen(resolver.get()) != 0) {
    gpr_log(GPR_INFO, "Warning: overriding resolver setting of %s",
            resolver.get());
  }
  if (resolver_type != nullptr && gpr_stricmp(resolver_type, "native") == 0) {
    GPR_GLOBAL_CONFIG_SET(grpc_dns_resolver, "native");
  } else if (resolver_type != nullptr &&
             gpr_stricmp(resolver_type, "ares") == 0) {
    GPR_GLOBAL_CONFIG_SET(grpc_dns_resolver, "ares");
  } else {
    gpr_log(GPR_ERROR, "--resolver_type was not set to ares or native");
    abort();
  }
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();

  {
    grpc_core::ExecCtx exec_ctx;
    test_named_and_numeric_scope_ids();
    // c-ares resolver doesn't support UDS (ability for native DNS resolver
    // to handle this is only expected to be used by servers, which
    // unconditionally use the native DNS resolver).
    grpc_core::UniquePtr<char> resolver =
        GPR_GLOBAL_CONFIG_GET(grpc_dns_resolver);
  }
  gpr_cmdline_destroy(cl);

  grpc_shutdown();
  return 0;
}
