//
//
// Copyright 2017 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <string>
#include <thread>
#include <vector>

#include <gmock/gmock.h>

#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/impl/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/event_engine/ares_resolver.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/load_balancing/grpclb/grpclb_balancer_addresses.h"
#include "src/core/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/resolver.h"
#include "src/core/resolver/resolver_registry.h"
#include "test/core/util/fake_udp_and_tcp_server.h"
#include "test/core/util/port.h"
#include "test/core/util/socket_use_after_close_detector.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/subprocess.h"
#include "test/cpp/util/test_config.h"

using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using std::vector;
using testing::UnorderedElementsAreArray;

ABSL_FLAG(std::string, target_name, "", "Target name to resolve.");
ABSL_FLAG(std::string, do_ordered_address_comparison, "",
          "Whether or not to compare resolved addresses to expected "
          "addresses using an ordered comparison. This is useful for "
          "testing certain behaviors that involve sorting of resolved "
          "addresses. Note it would be better if this argument was a "
          "bool flag, but it's a string for ease of invocation from "
          "the generated python test runner.");
ABSL_FLAG(std::string, expected_addrs, "",
          "List of expected backend or balancer addresses in the form "
          "'<ip0:port0>,<is_balancer0>;<ip1:port1>,<is_balancer1>;...'. "
          "'is_balancer' should be bool, i.e. true or false.");
ABSL_FLAG(std::string, expected_chosen_service_config, "",
          "Expected service config json string that gets chosen (no "
          "whitespace). Empty for none.");
ABSL_FLAG(std::string, expected_service_config_error, "",
          "Expected service config error. Empty for none.");
ABSL_FLAG(std::string, local_dns_server_address, "",
          "Optional. This address is placed as the uri authority if present.");
// TODO(Capstan): Is this worth making `bool` now with Abseil flags?
ABSL_FLAG(
    std::string, enable_srv_queries, "",
    "Whether or not to enable SRV queries for the ares resolver instance."
    "It would be better if this arg could be bool, but the way that we "
    "generate "
    "the python script runner doesn't allow us to pass a gflags bool to this "
    "binary.");
// TODO(Capstan): Is this worth making `bool` now with Abseil flags?
ABSL_FLAG(
    std::string, enable_txt_queries, "",
    "Whether or not to enable TXT queries for the ares resolver instance."
    "It would be better if this arg could be bool, but the way that we "
    "generate "
    "the python script runner doesn't allow us to pass a gflags bool to this "
    "binary.");
// TODO(Capstan): Is this worth making `bool` now with Abseil flags?
ABSL_FLAG(
    std::string, inject_broken_nameserver_list, "",
    "Whether or not to configure c-ares to use a broken nameserver list, in "
    "which "
    "the first nameserver in the list is non-responsive, but the second one "
    "works, i.e "
    "serves the expected DNS records; using for testing such a real scenario."
    "It would be better if this arg could be bool, but the way that we "
    "generate "
    "the python script runner doesn't allow us to pass a gflags bool to this "
    "binary.");
ABSL_FLAG(std::string, expected_lb_policy, "",
          "Expected lb policy name that appears in resolver result channel "
          "arg. Empty for none.");

namespace {

class GrpcLBAddress final {
 public:
  GrpcLBAddress(std::string address, bool is_balancer)
      : is_balancer(is_balancer), address(std::move(address)) {}

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
  while (!expected_addrs.empty()) {
    // get the next <ip>,<port> (v4 or v6)
    size_t next_comma = expected_addrs.find(',');
    if (next_comma == std::string::npos) {
      grpc_core::Crash(absl::StrFormat(
          "Missing ','. Expected_addrs arg should be a semicolon-separated "
          "list of <ip-port>,<bool> pairs. Left-to-be-parsed arg is |%s|",
          expected_addrs.c_str()));
    }
    std::string next_addr = expected_addrs.substr(0, next_comma);
    expected_addrs = expected_addrs.substr(next_comma + 1, std::string::npos);
    // get the next is_balancer 'bool' associated with this address
    size_t next_semicolon = expected_addrs.find(';');
    bool is_balancer = false;
    gpr_parse_bool_value(expected_addrs.substr(0, next_semicolon).c_str(),
                         &is_balancer);
    out.emplace_back(GrpcLBAddress(next_addr, is_balancer));
    if (next_semicolon == std::string::npos) {
      break;
    }
    expected_addrs =
        expected_addrs.substr(next_semicolon + 1, std::string::npos);
  }
  if (out.empty()) {
    grpc_core::Crash(
        "expected_addrs arg should be a semicolon-separated list of "
        "<ip-port>,<bool> pairs");
  }
  return out;
}

gpr_timespec TestDeadline(void) {
  return grpc_timeout_seconds_to_deadline(100);
}

struct ArgsStruct {
  gpr_event ev;
  gpr_mu* mu;
  bool done;              // guarded by mu
  grpc_pollset* pollset;  // guarded by mu
  grpc_pollset_set* pollset_set;
  std::shared_ptr<grpc_core::WorkSerializer> lock;
  grpc_channel_args* channel_args;
  vector<GrpcLBAddress> expected_addrs;
  std::string expected_service_config_string;
  std::string expected_service_config_error;
  std::string expected_lb_policy;
};

void ArgsInit(ArgsStruct* args) {
  gpr_event_init(&args->ev);
  args->pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(args->pollset, &args->mu);
  args->pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(args->pollset_set, args->pollset);
  args->lock = std::make_shared<grpc_core::WorkSerializer>(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  args->done = false;
  args->channel_args = nullptr;
}

void DoNothing(void* /*arg*/, grpc_error_handle /*error*/) {}

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
}

gpr_timespec NSecondDeadline(int seconds) {
  return gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                      gpr_time_from_seconds(seconds, GPR_TIMESPAN));
}

void PollPollsetUntilRequestDone(ArgsStruct* args) {
  // Use a 20-second timeout to give room for the tests that involve
  // a non-responsive name server (c-ares uses a ~5 second query timeout
  // for that server before succeeding with the healthy one).
  gpr_timespec deadline = NSecondDeadline(20);
  while (true) {
    grpc_core::MutexLockForGprMu lock(args->mu);
    if (args->done) {
      break;
    }
    gpr_timespec time_left =
        gpr_time_sub(deadline, gpr_now(GPR_CLOCK_REALTIME));
    gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRId64 ".%09d", args->done,
            time_left.tv_sec, time_left.tv_nsec);
    GPR_ASSERT(gpr_time_cmp(time_left, gpr_time_0(GPR_TIMESPAN)) >= 0);
    grpc_pollset_worker* worker = nullptr;
    grpc_core::ExecCtx exec_ctx;
    if (grpc_core::IsEventEngineDnsEnabled()) {
      // This essentially becomes a condition variable.
      GRPC_LOG_IF_ERROR(
          "pollset_work",
          grpc_pollset_work(
              args->pollset, &worker,
              grpc_core::Timestamp::FromTimespecRoundUp(deadline)));
    } else {
      GRPC_LOG_IF_ERROR(
          "pollset_work",
          grpc_pollset_work(
              args->pollset, &worker,
              grpc_core::Timestamp::FromTimespecRoundUp(NSecondDeadline(1))));
    }
  }
  gpr_event_set(&args->ev, reinterpret_cast<void*>(1));
}

void CheckServiceConfigResultLocked(const char* service_config_json,
                                    absl::Status service_config_error,
                                    ArgsStruct* args) {
  if (!args->expected_service_config_string.empty()) {
    ASSERT_NE(service_config_json, nullptr);
    EXPECT_EQ(service_config_json, args->expected_service_config_string);
  }
  if (args->expected_service_config_error.empty()) {
    EXPECT_TRUE(service_config_error.ok())
        << "Actual error: " << service_config_error.ToString();
  } else {
    EXPECT_THAT(service_config_error.ToString(),
                testing::HasSubstr(args->expected_service_config_error));
  }
}

void CheckLBPolicyResultLocked(const grpc_core::ChannelArgs channel_args,
                               ArgsStruct* args) {
  absl::optional<absl::string_view> lb_policy_arg =
      channel_args.GetString(GRPC_ARG_LB_POLICY_NAME);
  if (!args->expected_lb_policy.empty()) {
    EXPECT_TRUE(lb_policy_arg.has_value());
    EXPECT_EQ(*lb_policy_arg, args->expected_lb_policy);
  } else {
    EXPECT_FALSE(lb_policy_arg.has_value());
  }
}

class ResultHandler : public grpc_core::Resolver::ResultHandler {
 public:
  static std::unique_ptr<grpc_core::Resolver::ResultHandler> Create(
      ArgsStruct* args) {
    return std::unique_ptr<grpc_core::Resolver::ResultHandler>(
        new ResultHandler(args));
  }

  explicit ResultHandler(ArgsStruct* args) : args_(args) {}

  void ReportResult(grpc_core::Resolver::Result result) override {
    CheckResult(result);
    grpc_core::MutexLockForGprMu lock(args_->mu);
    GPR_ASSERT(!args_->done);
    args_->done = true;
    GRPC_LOG_IF_ERROR("pollset_kick",
                      grpc_pollset_kick(args_->pollset, nullptr));
  }

  virtual void CheckResult(const grpc_core::Resolver::Result& /*result*/) {}

 protected:
  ArgsStruct* args_struct() const { return args_; }

 private:
  ArgsStruct* args_;
};

class CheckingResultHandler : public ResultHandler {
 public:
  static std::unique_ptr<grpc_core::Resolver::ResultHandler> Create(
      ArgsStruct* args) {
    return std::unique_ptr<grpc_core::Resolver::ResultHandler>(
        new CheckingResultHandler(args));
  }

  explicit CheckingResultHandler(ArgsStruct* args) : ResultHandler(args) {}

  void CheckResult(const grpc_core::Resolver::Result& result) override {
    ASSERT_TRUE(result.addresses.ok()) << result.addresses.status().ToString();
    ArgsStruct* args = args_struct();
    std::vector<GrpcLBAddress> found_lb_addrs;
    AddActualAddresses(*result.addresses, /*is_balancer=*/false,
                       &found_lb_addrs);
    const grpc_core::EndpointAddressesList* balancer_addresses =
        grpc_core::FindGrpclbBalancerAddressesInChannelArgs(result.args);
    if (balancer_addresses != nullptr) {
      AddActualAddresses(*balancer_addresses, /*is_balancer=*/true,
                         &found_lb_addrs);
    }
    gpr_log(GPR_INFO,
            "found %" PRIdPTR " backend addresses and %" PRIdPTR
            " balancer addresses",
            result.addresses->size(),
            balancer_addresses == nullptr ? 0L : balancer_addresses->size());
    if (args->expected_addrs.size() != found_lb_addrs.size()) {
      grpc_core::Crash(absl::StrFormat("found lb addrs size is: %" PRIdPTR
                                       ". expected addrs size is %" PRIdPTR,
                                       found_lb_addrs.size(),
                                       args->expected_addrs.size()));
    }
    if (absl::GetFlag(FLAGS_do_ordered_address_comparison) == "True") {
      EXPECT_EQ(args->expected_addrs, found_lb_addrs);
    } else if (absl::GetFlag(FLAGS_do_ordered_address_comparison) == "False") {
      EXPECT_THAT(args->expected_addrs,
                  UnorderedElementsAreArray(found_lb_addrs));
    } else {
      gpr_log(GPR_ERROR,
              "Invalid for setting for --do_ordered_address_comparison. "
              "Have %s, want True or False",
              absl::GetFlag(FLAGS_do_ordered_address_comparison).c_str());
      GPR_ASSERT(0);
    }
    if (!result.service_config.ok()) {
      CheckServiceConfigResultLocked(nullptr, result.service_config.status(),
                                     args);
    } else if (*result.service_config == nullptr) {
      CheckServiceConfigResultLocked(nullptr, absl::OkStatus(), args);
    } else {
      CheckServiceConfigResultLocked(
          std::string((*result.service_config)->json_string()).c_str(),
          absl::OkStatus(), args);
    }
    if (args->expected_service_config_string.empty()) {
      CheckLBPolicyResultLocked(result.args, args);
    }
  }

 private:
  static void AddActualAddresses(
      const grpc_core::EndpointAddressesList& addresses, bool is_balancer,
      std::vector<GrpcLBAddress>* out) {
    for (size_t i = 0; i < addresses.size(); i++) {
      const grpc_core::EndpointAddresses& addr = addresses[i];
      std::string str =
          grpc_sockaddr_to_string(&addr.address(), true /* normalize */)
              .value();
      gpr_log(GPR_INFO, "%s", str.c_str());
      out->emplace_back(GrpcLBAddress(std::move(str), is_balancer));
    }
  }
};

int g_fake_non_responsive_dns_server_port = -1;

// This function will configure any ares_channel created by the c-ares based
// resolver. This is useful to effectively mock /etc/resolv.conf settings
// (and equivalent on Windows), which unit tests don't have write permissions.
//
void InjectBrokenNameServerList(ares_channel* channel) {
  struct ares_addr_port_node dns_server_addrs[2];
  memset(dns_server_addrs, 0, sizeof(dns_server_addrs));
  std::string unused_host;
  std::string local_dns_server_port;
  GPR_ASSERT(grpc_core::SplitHostPort(
      absl::GetFlag(FLAGS_local_dns_server_address).c_str(), &unused_host,
      &local_dns_server_port));
  gpr_log(GPR_DEBUG,
          "Injecting broken nameserver list. Bad server address:|[::1]:%d|. "
          "Good server address:%s",
          g_fake_non_responsive_dns_server_port,
          absl::GetFlag(FLAGS_local_dns_server_address).c_str());
  // Put the non-responsive DNS server at the front of c-ares's nameserver list.
  dns_server_addrs[0].family = AF_INET6;
  (reinterpret_cast<char*>(&dns_server_addrs[0].addr.addr6))[15] = 0x1;
  dns_server_addrs[0].tcp_port = g_fake_non_responsive_dns_server_port;
  dns_server_addrs[0].udp_port = g_fake_non_responsive_dns_server_port;
  dns_server_addrs[0].next = &dns_server_addrs[1];
  // Put the actual healthy DNS server after the first one. The expectation is
  // that the resolver will timeout the query to the non-responsive DNS server
  // and will skip over to this healthy DNS server, without causing any DNS
  // resolution errors.
  dns_server_addrs[1].family = AF_INET;
  (reinterpret_cast<char*>(&dns_server_addrs[1].addr.addr4))[0] = 0x7f;
  (reinterpret_cast<char*>(&dns_server_addrs[1].addr.addr4))[3] = 0x1;
  dns_server_addrs[1].tcp_port = atoi(local_dns_server_port.c_str());
  dns_server_addrs[1].udp_port = atoi(local_dns_server_port.c_str());
  dns_server_addrs[1].next = nullptr;
  GPR_ASSERT(ares_set_servers_ports(*channel, dns_server_addrs) ==
             ARES_SUCCESS);
}

void StartResolvingLocked(grpc_core::Resolver* r) { r->StartLocked(); }

void RunResolvesRelevantRecordsTest(
    std::unique_ptr<grpc_core::Resolver::ResultHandler> (*CreateResultHandler)(
        ArgsStruct* args),
    grpc_core::ChannelArgs resolver_args) {
  grpc_core::ExecCtx exec_ctx;
  ArgsStruct args;
  ArgsInit(&args);
  args.expected_addrs = ParseExpectedAddrs(absl::GetFlag(FLAGS_expected_addrs));
  args.expected_service_config_string =
      absl::GetFlag(FLAGS_expected_chosen_service_config);
  args.expected_service_config_error =
      absl::GetFlag(FLAGS_expected_service_config_error);
  args.expected_lb_policy = absl::GetFlag(FLAGS_expected_lb_policy);
  // maybe build the address with an authority
  std::string whole_uri;
  gpr_log(GPR_DEBUG,
          "resolver_component_test: --inject_broken_nameserver_list: %s",
          absl::GetFlag(FLAGS_inject_broken_nameserver_list).c_str());
  std::unique_ptr<grpc_core::testing::FakeUdpAndTcpServer>
      fake_non_responsive_dns_server;
  if (absl::GetFlag(FLAGS_inject_broken_nameserver_list) == "True") {
    fake_non_responsive_dns_server = std::make_unique<
        grpc_core::testing::FakeUdpAndTcpServer>(
        grpc_core::testing::FakeUdpAndTcpServer::AcceptMode::
            kWaitForClientToSendFirstBytes,
        grpc_core::testing::FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer);
    g_fake_non_responsive_dns_server_port =
        fake_non_responsive_dns_server->port();
    if (grpc_core::IsEventEngineDnsEnabled()) {
      event_engine_grpc_ares_test_only_inject_config =
          InjectBrokenNameServerList;
    } else {
      grpc_ares_test_only_inject_config = InjectBrokenNameServerList;
    }
    whole_uri = absl::StrCat("dns:///", absl::GetFlag(FLAGS_target_name));
  } else if (absl::GetFlag(FLAGS_inject_broken_nameserver_list) == "False") {
    gpr_log(GPR_INFO, "Specifying authority in uris to: %s",
            absl::GetFlag(FLAGS_local_dns_server_address).c_str());
    whole_uri = absl::StrFormat("dns://%s/%s",
                                absl::GetFlag(FLAGS_local_dns_server_address),
                                absl::GetFlag(FLAGS_target_name));
  } else {
    grpc_core::Crash("Invalid value for --inject_broken_nameserver_list.");
  }
  gpr_log(GPR_DEBUG, "resolver_component_test: --enable_srv_queries: %s",
          absl::GetFlag(FLAGS_enable_srv_queries).c_str());
  // By default, SRV queries are disabled, so tests that expect no SRV query
  // should avoid setting any channel arg. Test cases that do rely on the SRV
  // query must explicitly enable SRV though.
  if (absl::GetFlag(FLAGS_enable_srv_queries) == "True") {
    resolver_args = resolver_args.Set(GRPC_ARG_DNS_ENABLE_SRV_QUERIES, true);
  } else if (absl::GetFlag(FLAGS_enable_srv_queries) != "False") {
    grpc_core::Crash("Invalid value for --enable_srv_queries.");
  }
  gpr_log(GPR_DEBUG, "resolver_component_test: --enable_txt_queries: %s",
          absl::GetFlag(FLAGS_enable_txt_queries).c_str());
  // By default, TXT queries are disabled, so tests that expect no TXT query
  // should avoid setting any channel arg. Test cases that do rely on the TXT
  // query must explicitly enable TXT though.
  if (absl::GetFlag(FLAGS_enable_txt_queries) == "True") {
    // Unlike SRV queries, there isn't a channel arg specific to TXT records.
    // Rather, we use the resolver-agnostic "service config" resolution option,
    // for which c-ares has its own specific default value, which isn't
    // necessarily shared by other resolvers.
    resolver_args =
        resolver_args.Set(GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION, false);
  } else if (absl::GetFlag(FLAGS_enable_txt_queries) != "False") {
    grpc_core::Crash("Invalid value for --enable_txt_queries.");
  }
  resolver_args = resolver_args.SetObject(GetDefaultEventEngine());
  // create resolver and resolve
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      grpc_core::CoreConfiguration::Get().resolver_registry().CreateResolver(
          whole_uri.c_str(), resolver_args, args.pollset_set, args.lock,
          CreateResultHandler(&args));
  auto* resolver_ptr = resolver.get();
  args.lock->Run([resolver_ptr]() { StartResolvingLocked(resolver_ptr); },
                 DEBUG_LOCATION);
  grpc_core::ExecCtx::Get()->Flush();
  PollPollsetUntilRequestDone(&args);
  ArgsFinish(&args);
}

TEST(ResolverComponentTest, TestResolvesRelevantRecords) {
  RunResolvesRelevantRecordsTest(CheckingResultHandler::Create,
                                 grpc_core::ChannelArgs());
}

TEST(ResolverComponentTest, TestResolvesRelevantRecordsWithConcurrentFdStress) {
  grpc_core::testing::SocketUseAfterCloseDetector
      socket_use_after_close_detector;
  // Run the resolver test
  RunResolvesRelevantRecordsTest(ResultHandler::Create,
                                 grpc_core::ChannelArgs());
}

TEST(ResolverComponentTest, TestDoesntCrashOrHangWith1MsTimeout) {
  // Queries in this test could either complete successfully or time out
  // and show cancellation. This test doesn't care - we just care that the
  // query completes and doesn't crash, get stuck, leak, etc.
  RunResolvesRelevantRecordsTest(
      ResultHandler::Create,
      grpc_core::ChannelArgs().Set(GRPC_ARG_DNS_ARES_QUERY_TIMEOUT_MS, 1));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Need before TestEnvironment construct for --grpc_experiments flag at
  // least.
  grpc::testing::InitTest(&argc, &argv, true);
  grpc::testing::TestEnvironment env(&argc, argv);
  if (absl::GetFlag(FLAGS_target_name).empty()) {
    grpc_core::Crash("Missing target_name param.");
  }
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
