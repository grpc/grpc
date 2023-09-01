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

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "test/core/util/port.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(std::vector<std::string>, extra_client_flags, {},
          "Extra flags to pass to clients.");

ABSL_FLAG(std::vector<std::string>, extra_server_flags, {},
          "Extra flags to pass to server.");

int test_client(const char* root, const char* host, int port) {
  int status;
  pid_t cli;
  cli = fork();
  if (cli == 0) {
    std::vector<char*> args;
    std::string command = absl::StrCat(root, "/interop_client");
    args.push_back(const_cast<char*>(command.c_str()));
    std::string port_arg = absl::StrCat("--server_port=", port);
    args.push_back(const_cast<char*>(port_arg.c_str()));
    auto extra_client_flags = absl::GetFlag(FLAGS_extra_client_flags);
    for (size_t i = 0; i < extra_client_flags.size(); i++) {
      args.push_back(const_cast<char*>(extra_client_flags[i].c_str()));
    }
    args.push_back(nullptr);
    execv(args[0], args.data());
    return 1;
  }
  // wait for client
  gpr_log(GPR_INFO, "Waiting for client: %s", host);
  if (waitpid(cli, &status, 0) == -1) return 2;
  if (!WIFEXITED(status)) return 4;
  if (WEXITSTATUS(status)) return WEXITSTATUS(status);
  return 0;
}

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  char* me = argv[0];
  char* lslash = strrchr(me, '/');
  char root[1024];
  int port = grpc_pick_unused_port_or_die();
  int status;
  pid_t svr;
  int ret;
  int do_ipv6 = 1;
  // seed rng with pid, so we don't end up with the same random numbers as a
  // concurrently running test binary
  srand(getpid());
  if (!grpc_ipv6_loopback_available()) {
    gpr_log(GPR_INFO, "Can't bind to ::1.  Skipping IPv6 tests.");
    do_ipv6 = 0;
  }
  // figure out where we are
  if (lslash) {
    memcpy(root, me, lslash - me);
    root[lslash - me] = 0;
  } else {
    strcpy(root, ".");
  }
  // start the server
  svr = fork();
  if (svr == 0) {
    std::vector<char*> args;
    std::string command = absl::StrCat(root, "/interop_server");
    args.push_back(const_cast<char*>(command.c_str()));
    std::string port_arg = absl::StrCat("--port=", port);
    args.push_back(const_cast<char*>(port_arg.c_str()));
    auto extra_server_flags = absl::GetFlag(FLAGS_extra_server_flags);
    for (size_t i = 0; i < extra_server_flags.size(); i++) {
      args.push_back(const_cast<char*>(extra_server_flags[i].c_str()));
    }
    args.push_back(nullptr);
    execv(args[0], args.data());
    return 1;
  }
  // wait a little
  sleep(10);
  // start the clients
  ret = test_client(root, "127.0.0.1", port);
  if (ret != 0) return ret;
  ret = test_client(root, "::ffff:127.0.0.1", port);
  if (ret != 0) return ret;
  ret = test_client(root, "localhost", port);
  if (ret != 0) return ret;
  if (do_ipv6) {
    ret = test_client(root, "::1", port);
    if (ret != 0) return ret;
  }
  // wait for server
  gpr_log(GPR_INFO, "Waiting for server");
  kill(svr, SIGINT);
  if (waitpid(svr, &status, 0) == -1) return 2;
  if (!WIFEXITED(status)) return 4;
  if (WEXITSTATUS(status)) return WEXITSTATUS(status);
  return 0;
}
