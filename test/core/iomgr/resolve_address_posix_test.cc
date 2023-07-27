//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <net/if.h>
#include <string.h>
#include <sys/un.h>

#include <string>

#include <gtest/gtest.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "test/core/util/cmdline.h"
#include "test/core/util/test_config.h"

static gpr_timespec test_deadline(void) {
  return grpc_timeout_seconds_to_deadline(100);
}

typedef struct args_struct {
  grpc_core::Thread thd;
  gpr_event ev;
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
  args->done = false;
}

void args_finish(args_struct* args) {
  ASSERT_TRUE(gpr_event_wait(&args->ev, test_deadline()));
  args->thd.Join();
  // Don't need to explicitly destruct args->thd since
  // args is actually going to be destructed, not just freed
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

static grpc_core::Timestamp n_sec_deadline(int seconds) {
  return grpc_core::Timestamp::FromTimespecRoundUp(
      grpc_timeout_seconds_to_deadline(seconds));
}

static void actually_poll(void* argsp) {
  args_struct* args = static_cast<args_struct*>(argsp);
  grpc_core::Timestamp deadline = n_sec_deadline(10);
  while (true) {
    grpc_core::ExecCtx exec_ctx;
    {
      grpc_core::MutexLockForGprMu lock(args->mu);
      if (args->done) {
        break;
      }
      grpc_core::Duration time_left = deadline - grpc_core::Timestamp::Now();
      gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRId64, args->done,
              time_left.millis());
      ASSERT_GE(time_left, grpc_core::Duration::Zero());
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

namespace {

void MustSucceed(args_struct* args,
                 absl::StatusOr<std::vector<grpc_resolved_address>> result) {
  ASSERT_TRUE(result.ok());
  ASSERT_FALSE(result->empty());
  grpc_core::MutexLockForGprMu lock(args->mu);
  args->done = true;
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
}

}  // namespace

static void resolve_address_must_succeed(const char* target) {
  grpc_core::ExecCtx exec_ctx;
  args_struct args;
  args_init(&args);
  poll_pollset_until_request_done(&args);
  grpc_core::GetDNSResolver()->LookupHostname(
      [&args](absl::StatusOr<std::vector<grpc_resolved_address>> result) {
        MustSucceed(&args, std::move(result));
      },
      target, /*port number=*/"1", grpc_core::kDefaultDNSRequestTimeout,
      args.pollset_set,
      /*name_server=*/"");
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
  ASSERT_GT(strlen(arbitrary_interface_name), 0);
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

ABSL_FLAG(std::string, resolver, "", "Resolver type (ares or native)");

TEST(ResolveAddressUsingAresResolverPosixTest, MainTest) {
  // First set the resolver type based off of --resolver
  std::string resolver_type = absl::GetFlag(FLAGS_resolver);
  // In case that there are more than one argument on the command line,
  // --resolver will always be the first one, so only parse the first argument
  // (other arguments may be unknown to cl)
  grpc_core::ConfigVars::Overrides overrides;
  if (resolver_type == "native") {
    overrides.dns_resolver = "native";
  } else if (resolver_type == "ares") {
    overrides.dns_resolver = "ares";
  } else {
    gpr_log(GPR_ERROR, "--resolver was not set to ares or native");
    ASSERT_TRUE(false);
  }
  grpc_core::ConfigVars::SetOverrides(overrides);

  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;
    test_named_and_numeric_scope_ids();
  }
  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  absl::ParseCommandLine(argc, argv);
  return RUN_ALL_TESTS();
}
