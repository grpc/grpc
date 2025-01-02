// Copyright 2023 The gRPC Authors
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

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/proto/grpc/testing/control.pb.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/qps/benchmark_config.h"
#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/parse_json.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/test_credentials_provider.h"

ABSL_FLAG(std::string, loadtest_config, "",
          "Path to a gRPC benchmark loadtest scenario JSON file. See "
          "scenario_runner.py");

namespace grpc {
namespace testing {

static void RunScenario() {
  std::string json_str =
      grpc_core::testing::GetFileContents(absl::GetFlag(FLAGS_loadtest_config));
  Scenarios scenarios;
  ParseJson(json_str, "grpc.testing.Scenarios", &scenarios);
  LOG(INFO) << "Running " << scenarios.scenarios(0).name();
  const auto result =
      RunScenario(scenarios.scenarios(0).client_config(), 1,
                  scenarios.scenarios(0).server_config(), 1,
                  scenarios.scenarios(0).warmup_seconds(),
                  scenarios.scenarios(0).benchmark_seconds(), -2, "",
                  kInsecureCredentialsType, {}, false, 0);
  GetReporter()->ReportQPS(*result);
  GetReporter()->ReportLatency(*result);
  LOG(ERROR) << "Global Stats:\n"
             << StatsAsJson(grpc_core::global_stats().Collect().get());
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  grpc::testing::RunScenario();
  return 0;
}
