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
ABSL_FLAG(std::string, scenario_config, "default",
          "Use minimal stack/resource quote/secure server");
ABSL_FLAG(bool, memory_profiling, false,
          "Run memory profiling");  // not connected to anything yet

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

  /* Set configurations */
  std::string minstack_arg = "--nominstack";
  std::string secure_arg = "--nosecure";
  if (absl::GetFlag(FLAGS_scenario_config) == "secure") secure_arg = "--secure";
  if (absl::GetFlag(FLAGS_scenario_config) == "resource_quota") {
    secure_arg = "--secure";
    // add in resource quota parameter setting later
  }
  if (absl::GetFlag(FLAGS_scenario_config) == "minstack")
    minstack_arg = "--minstack";
  if (absl::GetFlag(FLAGS_size) == 0) absl::SetFlag(&FLAGS_size, 50000);

  /* per-call memory usage benchmark */
  if (absl::GetFlag(FLAGS_benchmark_name) == "call") {
    /* start the server */
    Subprocess svr({absl::StrCat(root, "/memory_usage_server",
                                 gpr_subprocess_binary_extension()),
                    "--bind", grpc_core::JoinHostPort("::", port), secure_arg,
                    minstack_arg});

    /* start the client */
    Subprocess cli({absl::StrCat(root, "/memory_usage_client",
                                 gpr_subprocess_binary_extension()),
                    "--target", grpc_core::JoinHostPort("127.0.0.1", port),
                    absl::StrCat("--warmup=", 10000),
                    absl::StrCat("--benchmark=", absl::GetFlag(FLAGS_size)),
                    minstack_arg});
    /* wait for completion */
    if ((status = cli.Join()) != 0) {
      printf("client failed with: %d", status);
      return 1;
    }

    svr.Interrupt();
    return svr.Join() == 0 ? 0 : 2;
  }

  return 5;
}
