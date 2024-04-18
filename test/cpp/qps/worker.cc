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

#include <signal.h>

#include <chrono>
#include <thread>
#include <vector>

#include "absl/flags/flag.h"

#include <grpc/grpc.h>
#include <grpc/support/time.h>

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/stats_data.h"
#include "test/core/util/test_config.h"
#include "test/cpp/qps/qps_worker.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/test_credentials_provider.h"

ABSL_FLAG(int32_t, driver_port, 0, "Port for communication with driver");
ABSL_FLAG(int32_t, server_port, 0,
          "Port for operation as a server, if not specified by the server "
          "config message");
ABSL_FLAG(std::string, credential_type, grpc::testing::kInsecureCredentialsType,
          "Credential type for communication with driver");

static bool got_sigint = false;

static void sigint_handler(int /*x*/) { got_sigint = true; }

namespace grpc {
namespace testing {

std::vector<grpc::testing::Server*>* g_inproc_servers = nullptr;

static void RunServer() {
  QpsWorker worker(absl::GetFlag(FLAGS_driver_port),
                   absl::GetFlag(FLAGS_server_port),
                   absl::GetFlag(FLAGS_credential_type));

  while (!got_sigint && !worker.Done()) {
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_millis(500, GPR_TIMESPAN)));
  }
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);

  signal(SIGINT, sigint_handler);

  grpc::testing::RunServer();
  gpr_log(GPR_ERROR, "Global Stats:\n%s",
          StatsAsJson(grpc_core::global_stats().Collect().get()).c_str());
  return 0;
}
