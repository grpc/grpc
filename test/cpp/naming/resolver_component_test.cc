/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <string.h>

#include <gflags/gflags.h>
#include <gmock/gmock.h>
#include <vector>

#include "test/cpp/util/subprocess.h"
#include "test/cpp/util/test_config.h"

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

using grpc::SubProcess;
using std::vector;
using testing::UnorderedElementsAreArray;

// Hack copied from "test/cpp/end2end/server_crash_test_client.cc"!
// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

DEFINE_string(target_name, "", "Target name to resolve.");
DEFINE_string(expected_addrs, "",
              "List of expected backend or balancer addresses in the form "
              "'<ip0:port0>,<is_balancer0>;<ip1:port1>,<is_balancer1>;...'. "
              "'is_balancer' should be bool, i.e. true or false.");
DEFINE_string(expected_chosen_service_config, "",
              "Expected service config json string that gets chosen (no "
              "whitespace). Empty for none.");
DEFINE_string(
    local_dns_server_address, "",
    "Optional. This address is placed as the uri authority if present.");
DEFINE_string(expected_lb_policy, "",
              "Expected lb policy name that appears in resolver result channel "
              "arg. Empty for none.");

namespace {

class GrpcLBAddress final {
 public:
  GrpcLBAddress(std::string address, bool is_balancer)
      : is_balancer(is_balancer), address(address) {}

  bool operator==(const GrpcLBAddress& other) const {
    return this->is_balancer == other.is_balancer &&
           this->address == other.address;
  }

  bool operator!=(const GrpcLBAddress& other) const {
    return !(*this == other);
  }

  bool is_balancer;
  std::string address;
};

vector<GrpcLBAddress> ParseExpectedAddrs(std::string expected_addrs) {
  std::vector<GrpcLBAddress> out;
  while (expected_addrs.size() != 0) {
    // get the next <ip>,<port> (v4 or v6)
    size_t next_comma = expected_addrs.find(",");
    if (next_comma == std::string::npos) {
      gpr_log(GPR_ERROR,
              "Missing ','. Expected_addrs arg should be a semicolon-separated "
              "list of <ip-port>,<bool> pairs. Left-to-be-parsed arg is |%s|",
              expected_addrs.c_str());
      abort();
    }
    std::string next_addr = expected_addrs.substr(0, next_comma);
    expected_addrs = expected_addrs.substr(next_comma + 1, std::string::npos);
    // get the next is_balancer 'bool' associated with this address
    size_t next_semicolon = expected_addrs.find(";");
    bool is_balancer =
        gpr_is_true(expected_addrs.substr(0, next_semicolon).c_str());
    out.emplace_back(GrpcLBAddress(next_addr, is_balancer));
    if (next_semicolon == std::string::npos) {
      break;
    }
    expected_addrs =
        expected_addrs.substr(next_semicolon + 1, std::string::npos);
  }
  if (out.size() == 0) {
    gpr_log(GPR_ERROR,
            "expected_addrs arg should be a semicolon-separated list of "
            "<ip-port>,<bool> pairs");
    abort();
  }
  return out;
}

gpr_timespec TestDeadline(void) {
  return grpc_timeout_seconds_to_deadline(100);
}

struct ArgsStruct {
  gpr_event ev;
  gpr_atm done_atm;
  gpr_mu* mu;
  grpc_pollset* pollset;
  grpc_pollset_set* pollset_set;
  grpc_combiner* lock;
  grpc_channel_args* channel_args;
  vector<GrpcLBAddress> expected_addrs;
  std::string expected_service_config_string;
  std::string expected_lb_policy;
};

void ArgsInit(ArgsStruct* args) {
  gpr_event_init(&args->ev);
  args->pollset = (grpc_pollset*)gpr_zalloc(grpc_pollset_size());
  grpc_pollset_init(args->pollset, &args->mu);
  args->pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(args->pollset_set, args->pollset);
  args->lock = grpc_combiner_create();
  gpr_atm_rel_store(&args->done_atm, 0);
  args->channel_args = nullptr;
}

void DoNothing(void* arg, grpc_error* error) {}

void ArgsFinish(ArgsStruct* args) {
  GPR_ASSERT(gpr_event_wait(&args->ev, TestDeadline()));
  grpc_pollset_set_del_pollset(args->pollset_set, args->pollset);
  grpc_pollset_set_destroy(args->pollset_set);
  grpc_closure DoNothing_cb;
  GRPC_CLOSURE_INIT(&DoNothing_cb, DoNothing, nullptr,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(args->pollset, &DoNothing_cb);
  // exec_ctx needs to be flushed before calling grpc_pollset_destroy()
  grpc_channel_args_destroy(args->channel_args);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_pollset_destroy(args->pollset);
  gpr_free(args->pollset);
  GRPC_COMBINER_UNREF(args->lock, nullptr);
}

gpr_timespec NSecondDeadline(int seconds) {
  return gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                      gpr_time_from_seconds(seconds, GPR_TIMESPAN));
}

void PollPollsetUntilRequestDone(ArgsStruct* args) {
  gpr_timespec deadline = NSecondDeadline(10);
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
    grpc_pollset_worker* worker = nullptr;
    grpc_core::ExecCtx exec_ctx;
    gpr_mu_lock(args->mu);
    GRPC_LOG_IF_ERROR("pollset_work",
                      grpc_pollset_work(args->pollset, &worker,
                                        grpc_timespec_to_millis_round_up(
                                            NSecondDeadline(1))));
    gpr_mu_unlock(args->mu);
  }
  gpr_event_set(&args->ev, (void*)1);
}

void CheckServiceConfigResultLocked(grpc_channel_args* channel_args,
                                    ArgsStruct* args) {
  const grpc_arg* service_config_arg =
      grpc_channel_args_find(channel_args, GRPC_ARG_SERVICE_CONFIG);
  if (args->expected_service_config_string != "") {
    GPR_ASSERT(service_config_arg != nullptr);
    GPR_ASSERT(service_config_arg->type == GRPC_ARG_STRING);
    EXPECT_EQ(service_config_arg->value.string,
              args->expected_service_config_string);
  } else {
    GPR_ASSERT(service_config_arg == nullptr);
  }
}

void CheckLBPolicyResultLocked(grpc_channel_args* channel_args,
                               ArgsStruct* args) {
  const grpc_arg* lb_policy_arg =
      grpc_channel_args_find(channel_args, GRPC_ARG_LB_POLICY_NAME);
  if (args->expected_lb_policy != "") {
    GPR_ASSERT(lb_policy_arg != nullptr);
    GPR_ASSERT(lb_policy_arg->type == GRPC_ARG_STRING);
    EXPECT_EQ(lb_policy_arg->value.string, args->expected_lb_policy);
  } else {
    GPR_ASSERT(lb_policy_arg == nullptr);
  }
}

void CheckResolverResultLocked(void* argsp, grpc_error* err) {
  ArgsStruct* args = (ArgsStruct*)argsp;
  grpc_channel_args* channel_args = args->channel_args;
  const grpc_arg* channel_arg =
      grpc_channel_args_find(channel_args, GRPC_ARG_LB_ADDRESSES);
  GPR_ASSERT(channel_arg != nullptr);
  GPR_ASSERT(channel_arg->type == GRPC_ARG_POINTER);
  grpc_lb_addresses* addresses =
      (grpc_lb_addresses*)channel_arg->value.pointer.p;
  gpr_log(GPR_INFO, "num addrs found: %" PRIdPTR ". expected %" PRIdPTR,
          addresses->num_addresses, args->expected_addrs.size());
  GPR_ASSERT(addresses->num_addresses == args->expected_addrs.size());
  std::vector<GrpcLBAddress> found_lb_addrs;
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    grpc_lb_address addr = addresses->addresses[i];
    char* str;
    grpc_sockaddr_to_string(&str, &addr.address, 1 /* normalize */);
    gpr_log(GPR_INFO, "%s", str);
    found_lb_addrs.emplace_back(
        GrpcLBAddress(std::string(str), addr.is_balancer));
    gpr_free(str);
  }
  if (args->expected_addrs.size() != found_lb_addrs.size()) {
    gpr_log(GPR_DEBUG,
            "found lb addrs size is: %" PRIdPTR
            ". expected addrs size is %" PRIdPTR,
            found_lb_addrs.size(), args->expected_addrs.size());
    abort();
  }
  EXPECT_THAT(args->expected_addrs, UnorderedElementsAreArray(found_lb_addrs));
  CheckServiceConfigResultLocked(channel_args, args);
  if (args->expected_service_config_string == "") {
    CheckLBPolicyResultLocked(channel_args, args);
  }
  gpr_atm_rel_store(&args->done_atm, 1);
  gpr_mu_lock(args->mu);
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
  gpr_mu_unlock(args->mu);
}

TEST(ResolverComponentTest, TestResolvesRelevantRecords) {
  grpc_core::ExecCtx exec_ctx;
  ArgsStruct args;
  ArgsInit(&args);
  args.expected_addrs = ParseExpectedAddrs(FLAGS_expected_addrs);
  args.expected_service_config_string = FLAGS_expected_chosen_service_config;
  args.expected_lb_policy = FLAGS_expected_lb_policy;
  // maybe build the address with an authority
  char* whole_uri = nullptr;
  GPR_ASSERT(asprintf(&whole_uri, "dns://%s/%s",
                      FLAGS_local_dns_server_address.c_str(),
                      FLAGS_target_name.c_str()));
  // create resolver and resolve
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      grpc_core::ResolverRegistry::CreateResolver(whole_uri, nullptr,
                                                  args.pollset_set, args.lock);
  gpr_free(whole_uri);
  grpc_closure on_resolver_result_changed;
  GRPC_CLOSURE_INIT(&on_resolver_result_changed, CheckResolverResultLocked,
                    (void*)&args, grpc_combiner_scheduler(args.lock));
  resolver->NextLocked(&args.channel_args, &on_resolver_result_changed);
  grpc_core::ExecCtx::Get()->Flush();
  PollPollsetUntilRequestDone(&args);
  ArgsFinish(&args);
}

}  // namespace

int main(int argc, char** argv) {
  grpc_init();
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_target_name == "") {
    gpr_log(GPR_ERROR, "Missing target_name param.");
    abort();
  }
  if (FLAGS_local_dns_server_address != "") {
    gpr_log(GPR_INFO, "Specifying authority in uris to: %s",
            FLAGS_local_dns_server_address.c_str());
  }
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
