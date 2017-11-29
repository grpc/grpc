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

#include <fstream>
#include <iostream>
#include <memory>
#include <set>

#include <grpc++/impl/codegen/config_protobuf.h>

#include <gflags/gflags.h>
#include <grpc/support/log.h>

#include "test/cpp/qps/benchmark_config.h"
#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/parse_json.h"
#include "test/cpp/qps/report.h"
#include "test/cpp/qps/server.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/test_credentials_provider.h"

DEFINE_string(scenarios_file, "",
              "JSON file containing an array of Scenario objects");
DEFINE_string(scenarios_json, "",
              "JSON string containing an array of Scenario objects");
DEFINE_bool(quit, false, "Quit the workers");
DEFINE_string(search_param, "",
              "The parameter, whose value is to be searched for to achieve "
              "targeted cpu load. For now, we have 'offered_load'. Later, "
              "'num_channels', 'num_outstanding_requests', etc. shall be "
              "added.");
DEFINE_double(
    initial_search_value, 0.0,
    "initial parameter value to start the search with (i.e. lower bound)");
DEFINE_double(targeted_cpu_load, 70.0,
              "Targeted cpu load (unit: %, range [0,100])");
DEFINE_double(stride, 1,
              "Defines each stride of the search. The larger the stride is, "
              "the coarser the result will be, but will also be faster.");
DEFINE_double(error_tolerance, 0.01,
              "Defines threshold for stopping the search. When current search "
              "range is narrower than the error_tolerance computed range, we "
              "stop the search.");

DEFINE_string(qps_server_target_override, "",
              "Override QPS server target to configure in client configs."
              "Only applicable if there is a single benchmark server.");

DEFINE_string(json_file_out, "", "File to write the JSON output to.");

DEFINE_string(credential_type, grpc::testing::kInsecureCredentialsType,
              "Credential type for communication with workers");
DEFINE_bool(run_inproc, false, "Perform an in-process transport test");

namespace grpc {
namespace testing {

static std::unique_ptr<ScenarioResult> RunAndReport(const Scenario& scenario,
                                                    bool* success) {
  std::cerr << "RUNNING SCENARIO: " << scenario.name() << "\n";
  auto result =
      RunScenario(scenario.client_config(), scenario.num_clients(),
                  scenario.server_config(), scenario.num_servers(),
                  scenario.warmup_seconds(), scenario.benchmark_seconds(),
                  !FLAGS_run_inproc ? scenario.spawn_local_worker_count() : -2,
                  FLAGS_qps_server_target_override, FLAGS_credential_type,
                  FLAGS_run_inproc);

  // Amend the result with scenario config. Eventually we should adjust
  // RunScenario contract so we don't need to touch the result here.
  result->mutable_scenario()->CopyFrom(scenario);

  GetReporter()->ReportQPS(*result);
  GetReporter()->ReportQPSPerCore(*result);
  GetReporter()->ReportLatency(*result);
  GetReporter()->ReportTimes(*result);
  GetReporter()->ReportCpuUsage(*result);
  GetReporter()->ReportPollCount(*result);
  GetReporter()->ReportQueriesPerCpuSec(*result);

  for (int i = 0; *success && i < result->client_success_size(); i++) {
    *success = result->client_success(i);
  }
  for (int i = 0; *success && i < result->server_success_size(); i++) {
    *success = result->server_success(i);
  }

  if (FLAGS_json_file_out != "") {
    std::ofstream json_outfile;
    json_outfile.open(FLAGS_json_file_out);
    json_outfile << "{\"qps\": " << result->summary().qps() << "}\n";
    json_outfile.close();
  }

  return result;
}

static double GetCpuLoad(Scenario* scenario, double offered_load,
                         bool* success) {
  scenario->mutable_client_config()
      ->mutable_load_params()
      ->mutable_poisson()
      ->set_offered_load(offered_load);
  auto result = RunAndReport(*scenario, success);
  return result->summary().server_cpu_usage();
}

static double BinarySearch(Scenario* scenario, double targeted_cpu_load,
                           double low, double high, bool* success) {
  while (low <= high * (1 - FLAGS_error_tolerance)) {
    double mid = low + (high - low) / 2;
    double current_cpu_load = GetCpuLoad(scenario, mid, success);
    gpr_log(GPR_DEBUG, "Binary Search: current_offered_load %.0f", mid);
    if (!*success) {
      gpr_log(GPR_ERROR, "Client/Server Failure");
      break;
    }
    if (targeted_cpu_load <= current_cpu_load) {
      high = mid - FLAGS_stride;
    } else {
      low = mid + FLAGS_stride;
    }
  }

  return low;
}

static double SearchOfferedLoad(double initial_offered_load,
                                double targeted_cpu_load, Scenario* scenario,
                                bool* success) {
  std::cerr << "RUNNING SCENARIO: " << scenario->name() << "\n";
  double current_offered_load = initial_offered_load;
  double current_cpu_load = GetCpuLoad(scenario, current_offered_load, success);
  if (current_cpu_load > targeted_cpu_load) {
    gpr_log(GPR_ERROR, "Initial offered load too high");
    return -1;
  }

  while (*success && (current_cpu_load < targeted_cpu_load)) {
    current_offered_load *= 2;
    current_cpu_load = GetCpuLoad(scenario, current_offered_load, success);
    gpr_log(GPR_DEBUG, "Binary Search: current_offered_load  %.0f",
            current_offered_load);
  }

  double targeted_offered_load =
      BinarySearch(scenario, targeted_cpu_load, current_offered_load / 2,
                   current_offered_load, success);

  return targeted_offered_load;
}

static bool QpsDriver() {
  grpc::string json;

  bool scfile = (FLAGS_scenarios_file != "");
  bool scjson = (FLAGS_scenarios_json != "");
  if ((!scfile && !scjson && !FLAGS_quit) ||
      (scfile && (scjson || FLAGS_quit)) || (scjson && FLAGS_quit)) {
    gpr_log(GPR_ERROR,
            "Exactly one of --scenarios_file, --scenarios_json, "
            "or --quit must be set");
    abort();
  }

  if (scfile) {
    // Read the json data from disk
    FILE* json_file = fopen(FLAGS_scenarios_file.c_str(), "r");
    GPR_ASSERT(json_file != nullptr);
    fseek(json_file, 0, SEEK_END);
    long len = ftell(json_file);
    char* data = new char[len];
    fseek(json_file, 0, SEEK_SET);
    GPR_ASSERT(len == (long)fread(data, 1, len, json_file));
    fclose(json_file);
    json = grpc::string(data, data + len);
    delete[] data;
  } else if (scjson) {
    json = FLAGS_scenarios_json.c_str();
  } else if (FLAGS_quit) {
    return RunQuit(FLAGS_credential_type);
  }

  // Parse into an array of scenarios
  Scenarios scenarios;
  ParseJson(json.c_str(), "grpc.testing.Scenarios", &scenarios);
  bool success = true;

  // Make sure that there is at least some valid scenario here
  GPR_ASSERT(scenarios.scenarios_size() > 0);

  for (int i = 0; i < scenarios.scenarios_size(); i++) {
    if (FLAGS_search_param == "") {
      const Scenario& scenario = scenarios.scenarios(i);
      RunAndReport(scenario, &success);
    } else {
      if (FLAGS_search_param == "offered_load") {
        Scenario* scenario = scenarios.mutable_scenarios(i);
        double targeted_offered_load =
            SearchOfferedLoad(FLAGS_initial_search_value,
                              FLAGS_targeted_cpu_load, scenario, &success);
        gpr_log(GPR_INFO, "targeted_offered_load %f", targeted_offered_load);
        GetCpuLoad(scenario, targeted_offered_load, &success);
      } else {
        gpr_log(GPR_ERROR, "Unimplemented search param");
      }
    }
  }
  return success;
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);

  bool ok = grpc::testing::QpsDriver();

  return ok ? 0 : 1;
}
