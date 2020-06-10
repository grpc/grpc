/*
 *
 * Copyright 2015-2016 gRPC authors.
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

#include <signal.h>
#include <string.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#ifdef __FreeBSD__
#include <sys/wait.h>
#endif

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/tmpfile.h"
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

static std::string CreateTmpFileForPort(const char* prefix) {
  char* tmp_filename;
  fclose(gpr_tmpfile(prefix, &tmp_filename));
  std::string result = tmp_filename;
  gpr_free(tmp_filename);
  return result;
}

static std::vector<int> WaitsForPortsResolved(std::vector<std::string> paths,
                                              int timeout_seconds) {
  std::vector<int> resolved_ports(paths.size());
  gpr_timespec deadline =
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                   gpr_time_from_seconds(timeout_seconds, GPR_TIMESPAN));
  while (true) {
    bool need_to_wait = false;
    for (int i = 0, i_end = int(resolved_ports.size()); i < i_end; i++) {
      if (resolved_ports[i] == 0) {
        int port = 0;
        std::ifstream port_file(paths[i]);
        port_file >> port;
        if (port != 0) {
          resolved_ports[i] = port;
          port_file.close();
          remove(paths[i].c_str());
        }
      }
      if (resolved_ports[i] == 0) {
        need_to_wait = true;
      }
    }
    if (!need_to_wait) {
      return resolved_ports;
    }
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_millis(100, GPR_TIMESPAN)));
    if (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), deadline) > 0) {
      return {};
    }
  }
}

int main(int argc, char** argv) {
  register_sighandler();

  std::string my_bin = argv[0];
  std::string bin_dir = my_bin.substr(0, my_bin.rfind('/'));

  std::vector<std::string> port_result_paths;
  for (int i = 0; i < kNumWorkers; i++) {
    std::string port_result_path = CreateTmpFileForPort("qps_worker_port");
    std::vector<std::string> args = {bin_dir + "/qps_worker", "--driver_port",
                                     "0", "--driver_port_result_path",
                                     port_result_path};
    g_workers[i] = new SubProcess(args);
    port_result_paths.push_back(port_result_path);
  }

  std::vector<int> resolved_ports =
      WaitsForPortsResolved(port_result_paths, 10);
  if (resolved_ports.size() < port_result_paths.size()) {
    gpr_log(GPR_ERROR, "Timeout in resolving ports from workers");
    return 1;
  }

  std::ostringstream env;
  for (int i = 0, i_end = int(resolved_ports.size()); i < i_end; i++) {
    if (i != 0) env << ",";
    env << "localhost:" << resolved_ports[i];
  }
  gpr_setenv("QPS_WORKERS", env.str().c_str());
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

  if (g_driver != nullptr) {
    delete g_driver;
  }
  g_driver = nullptr;
  for (int i = 0; i < kNumWorkers; ++i) {
    if (g_workers[i] != nullptr) {
      delete g_workers[i];
    }
  }
  GPR_ASSERT(driver_join_status == 0);
}
