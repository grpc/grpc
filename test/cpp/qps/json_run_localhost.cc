//
//
// Copyright 2015-2016 gRPC authors.
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

#include <signal.h>
#include <string.h>

#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#ifdef __FreeBSD__
#include <sys/wait.h>
#endif

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/env.h"
#include "test/core/util/port.h"
#include "test/cpp/util/subprocess.h"

using grpc::SubProcess;

constexpr auto kNumWorkers = 2;

static SubProcess* g_driver;
static SubProcess* g_workers[kNumWorkers];

template <class T>
std::string as_string(const T& val) {
  std::ostringstream out;
  out << val;
  return out.str();
}

static void sighandler(int /*sig*/) {
  const int errno_saved = errno;
  if (g_driver != nullptr) g_driver->Interrupt();
  for (int i = 0; i < kNumWorkers; ++i) {
    if (g_workers[i]) g_workers[i]->Interrupt();
  }
  errno = errno_saved;
}

static void register_sighandler() {
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = sighandler;

  sigaction(SIGINT, &act, nullptr);
  sigaction(SIGTERM, &act, nullptr);
}

static void LogStatus(int status, const char* label) {
  if (WIFEXITED(status)) {
    gpr_log(GPR_INFO, "%s: subprocess exited with status %d", label,
            WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    gpr_log(GPR_INFO, "%s: subprocess terminated with signal %d", label,
            WTERMSIG(status));
  } else {
    gpr_log(GPR_INFO, "%s: unknown subprocess status: %d", label, status);
  }
}

int main(int argc, char** argv) {
  register_sighandler();

  std::string my_bin = argv[0];
  std::string bin_dir = my_bin.substr(0, my_bin.rfind('/'));

  std::ostringstream env;
  bool first = true;

  for (int i = 0; i < kNumWorkers; i++) {
    const auto driver_port = grpc_pick_unused_port_or_die();
    // ServerPort can be used or not later depending on the type of worker
    // but we like to issue all ports required here to avoid port conflict.
    const auto server_port = grpc_pick_unused_port_or_die();
    std::vector<std::string> args = {bin_dir + "/qps_worker", "-driver_port",
                                     as_string(driver_port), "-server_port",
                                     as_string(server_port)};
    g_workers[i] = new SubProcess(args);
    if (!first) env << ",";
    env << "localhost:" << driver_port;
    first = false;
  }

  grpc_core::SetEnv("QPS_WORKERS", env.str().c_str());
  std::vector<std::string> args = {bin_dir + "/qps_json_driver"};
  for (int i = 1; i < argc; i++) {
    args.push_back(argv[i]);
  }

  g_driver = new SubProcess(args);
  const int driver_join_status = g_driver->Join();
  if (driver_join_status != 0) {
    LogStatus(driver_join_status, "driver");
  }
  for (int i = 0; i < kNumWorkers; ++i) {
    if (g_workers[i]) g_workers[i]->Interrupt();
  }

  for (int i = 0; i < kNumWorkers; ++i) {
    if (g_workers[i]) {
      const int worker_status = g_workers[i]->Join();
      if (worker_status != 0) {
        LogStatus(worker_status, "worker");
      }
    }
  }

  delete g_driver;

  g_driver = nullptr;
  for (int i = 0; i < kNumWorkers; ++i) {
    if (g_workers[i] != nullptr) {
      delete g_workers[i];
    }
  }
  GPR_ASSERT(driver_join_status == 0);
}
