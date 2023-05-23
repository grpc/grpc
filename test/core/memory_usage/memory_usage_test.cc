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

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/host_port.h"
#include "test/core/util/port.h"
#include "test/core/util/subprocess.h"
#include "test/core/util/test_config.h"

ABSL_FLAG(std::string, benchmark_names, "call,channel",
          "Which benchmark to run");  // Default all benchmarks in order to
                                      // trigger CI testing for each one
ABSL_FLAG(int, size, 1000, "Number of channels/calls");
ABSL_FLAG(std::string, scenario_config, "insecure",
          "Possible Values: minstack (Use minimal stack), resource_quota, "
          "secure (Use SSL credentials on server)");
ABSL_FLAG(bool, memory_profiling, false,
          "Run memory profiling");  // TODO (chennancy) Connect this flag

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
int RunCallBenchmark(char* root, std::vector<std::string> server_scenario_flags,
                     std::vector<std::string> client_scenario_flags) {
  int status;
  int port = grpc_pick_unused_port_or_die();

  // start the server
  std::vector<std::string> server_flags = {
      absl::StrCat(root, "/memory_usage_server",
                   gpr_subprocess_binary_extension()),
      "--grpc_experiments",
      std::string(grpc_core::ConfigVars::Get().Experiments()), "--bind",
      grpc_core::JoinHostPort("::", port)};
  // Add scenario-specific server flags to the end of the server_flags
  absl::c_move(server_scenario_flags, std::back_inserter(server_flags));
  Subprocess svr(server_flags);

  // start the client
  std::vector<std::string> client_flags = {
      absl::StrCat(root, "/memory_usage_client",
                   gpr_subprocess_binary_extension()),
      "--target",
      grpc_core::JoinHostPort("127.0.0.1", port),
      "--grpc_experiments",
      std::string(grpc_core::ConfigVars::Get().Experiments()),
      absl::StrCat("--warmup=", 10000),
      absl::StrCat("--benchmark=", absl::GetFlag(FLAGS_size))};
  // Add scenario-specific client flags to the end of the client_flags
  absl::c_move(client_scenario_flags, std::back_inserter(client_flags));
  Subprocess cli(client_flags);
  // wait for completion
  if ((status = cli.Join()) != 0) {
    printf("client failed with: %d", status);
    return 1;
  }

  svr.Interrupt();
  return svr.Join() == 0 ? 0 : 2;
}

// Per-channel benchmark
int RunChannelBenchmark(char* root) {
  // TODO(chennancy) Add the scenario specific flags
  int status;
  int port = grpc_pick_unused_port_or_die();

  // start the server
  std::vector<std::string> server_flags = {
      absl::StrCat(root, "/memory_usage_callback_server",
                   gpr_subprocess_binary_extension()),
      "--bind", grpc_core::JoinHostPort("::", port)};
  Subprocess svr(server_flags);

  // Wait one second before starting client to avoid possible race condition
  // of client sending an RPC before the server is set up
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));

  // start the client
  std::vector<std::string> client_flags = {
      absl::StrCat(root, "/memory_usage_callback_client",
                   gpr_subprocess_binary_extension()),
      "--target",
      grpc_core::JoinHostPort("127.0.0.1", port),
      "--nosecure",
      absl::StrCat("--server_pid=", svr.GetPID()),
      absl::StrCat("--size=", absl::GetFlag(FLAGS_size))};
  Subprocess cli(client_flags);
  // wait for completion
  if ((status = cli.Join()) != 0) {
    printf("client failed with: %d", status);
    return 1;
  }
  svr.Interrupt();
  return svr.Join() == 0 ? 0 : 2;
}

int RunBenchmark(char* root, absl::string_view benchmark,
                 std::vector<std::string> server_scenario_flags,
                 std::vector<std::string> client_scenario_flags) {
  if (benchmark == "call") {
    return RunCallBenchmark(root, server_scenario_flags, client_scenario_flags);
  } else if (benchmark == "channel") {
    return RunChannelBenchmark(root);
  } else {
    gpr_log(GPR_INFO, "Not a valid benchmark name");
    return 4;
  }
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
  auto benchmarks = absl::StrSplit(absl::GetFlag(FLAGS_benchmark_names), ',');
  for (const auto& benchmark : benchmarks) {
    int r = RunBenchmark(root, benchmark, it_scenario->second.server,
                         it_scenario->second.client);
    if (r != 0) return r;
  }
  return 0;
}
