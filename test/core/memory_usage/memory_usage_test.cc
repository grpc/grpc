//
//
// Copyright 2015 gRPC authors.
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/wrappers.pb.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gpr/subprocess.h"
#include "src/core/lib/gprpp/env.h"
#include "src/proto/grpc/testing/xds/v3/cluster.pb.h"
#include "src/proto/grpc/testing/xds/v3/health_check.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/resolve_localhost_ip46.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/xds/xds_server.h"
#include "test/cpp/end2end/xds/xds_utils.h"

using grpc::testing::XdsResourceUtils;

// Default all benchmarks in order to trigger CI testing for each one
ABSL_FLAG(std::string, benchmark_names, "",
          "Which benchmark to run.  If empty, defaults to 'call,channel' "
          "if --use_xds is false, or 'call,channel,channel_multi_address' "
          "if --use_xds is true.");

ABSL_FLAG(int, size, 1000, "Number of channels/calls");
ABSL_FLAG(std::string, scenario_config, "insecure",
          "Possible Values: minstack (Use minimal stack), resource_quota, "
          "secure (Use SSL credentials on server)");
ABSL_FLAG(bool, memory_profiling, false,
          "Run memory profiling");  // TODO (chennancy) Connect this flag
ABSL_FLAG(bool, use_xds, false, "Use xDS");

// TODO(roth, ctiller): Add support for multiple addresses per channel.

class Subprocess {
 public:
  explicit Subprocess(std::vector<std::string> args) {
    std::vector<const char*> args_c;
    args_c.reserve(args.size());
    for (const auto& arg : args) {
      args_c.push_back(arg.c_str());
    }
    process_ = gpr_subprocess_create(args_c.size(), args_c.data());
  }

  int GetPID() { return gpr_subprocess_get_process_id(process_); }
  int Join() { return gpr_subprocess_join(process_); }
  void Interrupt() { gpr_subprocess_interrupt(process_); }

  ~Subprocess() { gpr_subprocess_destroy(process_); }

 private:
  gpr_subprocess* process_;
};

// per-call memory usage benchmark
int RunCallBenchmark(int port, char* root,
                     std::vector<std::string> server_scenario_flags,
                     std::vector<std::string> client_scenario_flags) {
  int status;

  // start the server
  gpr_log(GPR_INFO, "starting server");
  std::vector<std::string> server_flags = {
      absl::StrCat(root, "/memory_usage_server",
                   gpr_subprocess_binary_extension()),
      "--grpc_experiments",
      std::string(grpc_core::ConfigVars::Get().Experiments()), "--bind",
      grpc_core::LocalIpAndPort(port)};
  if (absl::GetFlag(FLAGS_use_xds)) server_flags.emplace_back("--use_xds");
  // Add scenario-specific server flags to the end of the server_flags
  absl::c_move(server_scenario_flags, std::back_inserter(server_flags));
  Subprocess svr(server_flags);
  gpr_log(GPR_INFO, "server started, pid %d", svr.GetPID());

  // Wait one second before starting client to give the server a chance
  // to start up.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));

  // start the client
  gpr_log(GPR_INFO, "starting client");
  std::vector<std::string> client_flags = {
      absl::StrCat(root, "/memory_usage_client",
                   gpr_subprocess_binary_extension()),
      "--target",
      absl::GetFlag(FLAGS_use_xds)
          ? absl::StrCat("xds:", XdsResourceUtils::kServerName)
          : grpc_core::LocalIpAndPort(port),
      "--grpc_experiments",
      std::string(grpc_core::ConfigVars::Get().Experiments()),
      absl::StrCat("--warmup=", 10000),
      absl::StrCat("--benchmark=", absl::GetFlag(FLAGS_size))};
  // Add scenario-specific client flags to the end of the client_flags
  absl::c_move(client_scenario_flags, std::back_inserter(client_flags));
  Subprocess cli(client_flags);
  gpr_log(GPR_INFO, "client started, pid %d", cli.GetPID());
  // wait for completion
  if ((status = cli.Join()) != 0) {
    printf("client failed with: %d", status);
    return 1;
  }

  svr.Interrupt();
  return svr.Join() == 0 ? 0 : 2;
}

// Per-channel benchmark
int RunChannelBenchmark(const std::vector<int>& server_ports, char* root) {
  // TODO(chennancy) Add the scenario specific flags

  // start the servers
  std::vector<Subprocess> servers;
  servers.reserve(server_ports.size());
  for (int port : server_ports) {
    gpr_log(GPR_INFO, "starting server on port %d", port);
    std::vector<std::string> server_flags = {
        absl::StrCat(root, "/memory_usage_callback_server",
                     gpr_subprocess_binary_extension()),
        "--bind", grpc_core::LocalIpAndPort(port)};
    if (absl::GetFlag(FLAGS_use_xds)) server_flags.emplace_back("--use_xds");
    servers.emplace_back(server_flags);
    gpr_log(GPR_INFO, "server started, pid %d", servers.back().GetPID());
  }

  // Wait one second before starting client to avoid possible race condition
  // of client sending an RPC before the server is set up
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));

  // start the client
  gpr_log(GPR_INFO, "starting client");
  std::vector<std::string> client_flags = {
      absl::StrCat(root, "/memory_usage_callback_client",
                   gpr_subprocess_binary_extension()),
      "--target",
      absl::GetFlag(FLAGS_use_xds)
          ? absl::StrCat("xds:", XdsResourceUtils::kServerName)
          : grpc_core::LocalIpAndPort(server_ports[0]),
      "--nosecure", absl::StrCat("--size=", absl::GetFlag(FLAGS_size))};
  if (server_ports.size() == 1) {
    client_flags.emplace_back(
        absl::StrCat("--server_pid=", servers[0].GetPID()));
  }
  Subprocess cli(client_flags);
  gpr_log(GPR_INFO, "client started, pid %d", cli.GetPID());
  // wait for completion
  int retval = cli.Join();
  if (retval != 0) {
    printf("client failed with: %d", retval);
    return 1;
  }
  for (auto& server : servers) {
    server.Interrupt();
    if (server.Join() != 0) retval = 2;
  }
  return retval;
}

struct XdsServer {
  std::shared_ptr<grpc::testing::AdsServiceImpl> ads_service;
  std::unique_ptr<grpc::Server> server;
};

XdsServer StartXdsServerAndConfigureBootstrap(
    const std::vector<int>& server_ports) {
  XdsServer xds_server;
  int xds_server_port = grpc_pick_unused_port_or_die();
  gpr_log(GPR_INFO, "xDS server port: %d", xds_server_port);
  // Generate xDS bootstrap and set the env var.
  std::string bootstrap =
      grpc::testing::XdsBootstrapBuilder()
          .SetDefaultServer(absl::StrCat("localhost:", xds_server_port))
          .SetXdsChannelCredentials("insecure")
          .Build();
  grpc_core::SetEnv("GRPC_XDS_BOOTSTRAP_CONFIG", bootstrap);
  gpr_log(GPR_INFO, "xDS bootstrap: %s", bootstrap.c_str());
  // Create ADS service.
  xds_server.ads_service = std::make_shared<grpc::testing::AdsServiceImpl>();
  xds_server.ads_service->Start();
  // Populate xDS resources.
  std::vector<XdsResourceUtils::EdsResourceArgs::Endpoint> endpoints;
  endpoints.reserve(server_ports.size());
  for (int port : server_ports) {
    endpoints.emplace_back(port);
    XdsResourceUtils::SetServerListenerNameAndRouteConfiguration(
        xds_server.ads_service.get(), XdsResourceUtils::DefaultServerListener(),
        port, XdsResourceUtils::DefaultServerRouteConfig());
  }
  XdsResourceUtils::SetListenerAndRouteConfiguration(
      xds_server.ads_service.get(), XdsResourceUtils::DefaultListener(),
      XdsResourceUtils::DefaultRouteConfig());
  auto cluster = XdsResourceUtils::DefaultCluster();
  cluster.mutable_circuit_breakers()
      ->add_thresholds()
      ->mutable_max_requests()
      ->set_value(std::numeric_limits<uint32_t>::max());
  xds_server.ads_service->SetCdsResource(cluster);
  xds_server.ads_service->SetEdsResource(
      XdsResourceUtils::BuildEdsResource(XdsResourceUtils::EdsResourceArgs(
          {XdsResourceUtils::EdsResourceArgs::Locality(
              "here", std::move(endpoints))})));
  // Create and start server.
  gpr_log(GPR_INFO, "starting xDS server...");
  grpc::ServerBuilder builder;
  builder.RegisterService(xds_server.ads_service.get());
  builder.AddListeningPort(absl::StrCat("localhost:", xds_server_port),
                           grpc::InsecureServerCredentials());
  xds_server.server = builder.BuildAndStart();
  gpr_log(GPR_INFO, "xDS server started");
  return xds_server;
}

int RunBenchmark(char* root, absl::string_view benchmark,
                 std::vector<std::string> server_scenario_flags,
                 std::vector<std::string> client_scenario_flags) {
  gpr_log(GPR_INFO, "running benchmark: %s", std::string(benchmark).c_str());
  const size_t num_ports = benchmark == "channel_multi_address" ? 10 : 1;
  std::vector<int> server_ports;
  server_ports.reserve(num_ports);
  for (size_t i = 0; i < num_ports; ++i) {
    server_ports.push_back(grpc_pick_unused_port_or_die());
  }
  gpr_log(GPR_INFO, "server ports: %s",
          absl::StrJoin(server_ports, ",").c_str());
  XdsServer xds_server;
  if (absl::GetFlag(FLAGS_use_xds)) {
    xds_server = StartXdsServerAndConfigureBootstrap(server_ports);
  }
  int retval;
  if (benchmark == "call") {
    retval = RunCallBenchmark(server_ports[0], root, server_scenario_flags,
                              client_scenario_flags);
  } else if (benchmark == "channel" || benchmark == "channel_multi_address") {
    retval = RunChannelBenchmark(server_ports, root);
  } else {
    gpr_log(GPR_INFO, "Not a valid benchmark name");
    retval = 4;
  }
  if (xds_server.server != nullptr) xds_server.server->Shutdown();
  gpr_log(GPR_INFO, "done running benchmark");
  return retval;
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  char* me = argv[0];
  char* lslash = strrchr(me, '/');
  char root[1024];

  std::vector<const char*> args;
  // figure out where we are
  if (lslash) {
    memcpy(root, me, static_cast<size_t>(lslash - me));
    root[lslash - me] = 0;
  } else {
    strcpy(root, ".");
  }

  // Set configurations based off scenario_config
  struct ScenarioArgs {
    std::vector<std::string> client;
    std::vector<std::string> server;
  };
  // TODO(chennancy): add in resource quota parameter setting later
  const std::map<std::string /*scenario*/, ScenarioArgs> scenarios = {
      {"secure", {/*client=*/{}, /*server=*/{"--secure"}}},
      {"resource_quota", {/*client=*/{}, /*server=*/{"--secure"}}},
      {"minstack", {/*client=*/{"--minstack"}, /*server=*/{"--minstack"}}},
      {"insecure", {{}, {}}},
  };
  auto it_scenario = scenarios.find(absl::GetFlag(FLAGS_scenario_config));
  if (it_scenario == scenarios.end()) {
    printf("No scenario matching the name could be found\n");
    return 3;
  }

  // Run all benchmarks listed (Multiple benchmarks usually only for default
  // scenario)
  std::string benchmark_names = absl::GetFlag(FLAGS_benchmark_names);
  if (benchmark_names.empty()) {
    benchmark_names = absl::GetFlag(FLAGS_use_xds)
                          ? "call,channel,channel_multi_address"
                          : "call,channel";
  }
  auto benchmarks = absl::StrSplit(benchmark_names, ',');
  grpc_init();
  for (const auto& benchmark : benchmarks) {
    int r = RunBenchmark(root, benchmark, it_scenario->second.server,
                         it_scenario->second.client);
    if (r != 0) return r;
  }
  grpc_shutdown();
  return 0;
}
