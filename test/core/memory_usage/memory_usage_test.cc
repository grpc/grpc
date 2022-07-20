/*
 *
 * Copyright 2015 gRPC authors.
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

#include <stdio.h>
#include <string.h>

#include <map>

#include "absl/algorithm/container.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "test/core/util/port.h"
#include "test/core/util/subprocess.h"

ABSL_FLAG(std::string, benchmark_name, "call", "Which benchmark to run");
ABSL_FLAG(int, size, 50000, "Number of channels/calls");
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

  int Join() { return gpr_subprocess_join(process_); }
  void Interrupt() { gpr_subprocess_interrupt(process_); }

  ~Subprocess() { gpr_subprocess_destroy(process_); }

 private:
  gpr_subprocess* process_;
};

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  char* me = argv[0];
  char* lslash = strrchr(me, '/');
  char root[1024];
  int port = grpc_pick_unused_port_or_die();
  std::vector<const char*> args;
  int status;
  /* figure out where we are */
  if (lslash) {
    memcpy(root, me, static_cast<size_t>(lslash - me));
    root[lslash - me] = 0;
  } else {
    strcpy(root, ".");
  }

  /* Set configurations based off scenario_config*/
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

  /* per-call memory usage benchmark */
  if (absl::GetFlag(FLAGS_benchmark_name) == "call") {
    /* start the server */
    std::vector<std::string> server_flags = {
        absl::StrCat(root, "/memory_usage_server",
                     gpr_subprocess_binary_extension()),
        "--bind", grpc_core::JoinHostPort("::", port)};
    // Add scenario-specific server flags to the end of the server_flags
    absl::c_move(it_scenario->second.server, std::back_inserter(server_flags));
    Subprocess svr(server_flags);

    /* start the client */
    std::vector<std::string> client_flags = {
        absl::StrCat(root, "/memory_usage_client",
                     gpr_subprocess_binary_extension()),
        "--target", grpc_core::JoinHostPort("127.0.0.1", port),
        absl::StrCat("--warmup=", 10000),
        absl::StrCat("--benchmark=", absl::GetFlag(FLAGS_size))};
    // Add scenario-specific client flags to the end of the client_flags
    absl::c_move(it_scenario->second.client, std::back_inserter(client_flags));
    Subprocess cli(client_flags);
    /* wait for completion */
    if ((status = cli.Join()) != 0) {
      printf("client failed with: %d", status);
      return 1;
    }

    svr.Interrupt();
    return svr.Join() == 0 ? 0 : 2;
  }

  printf("Command line args couldn't be parsed\n");
  return 4;
}
