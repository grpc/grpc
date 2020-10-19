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

#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/config_protobuf.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <set>

#include "absl/flags/flag.h"
#include "test/core/util/test_config.h"
#include "test/cpp/qps/benchmark_config.h"
#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/parse_json.h"
#include "test/cpp/qps/report.h"
#include "test/cpp/qps/server.h"
#include "test/cpp/util/test_config.h"
#include "test/cpp/util/test_credentials_provider.h"

ABSL_FLAG(std::string, scenarios_file, "",
          "JSON file containing an array of Scenario objects");
ABSL_FLAG(std::string, scenarios_json, "",
          "JSON string containing an array of Scenario objects");
ABSL_FLAG(bool, quit, false, "Quit the workers");
ABSL_FLAG(std::string, search_param, "",
          "The parameter, whose value is to be searched for to achieve "
          "targeted cpu load. For now, we have 'offered_load'. Later, "
          "'num_channels', 'num_outstanding_requests', etc. shall be "
          "added.");
ABSL_FLAG(
    double, initial_search_value, 0.0,
    "initial parameter value to start the search with (i.e. lower bound)");
ABSL_FLAG(double, targeted_cpu_load, 70.0,
          "Targeted cpu load (unit: %, range [0,100])");
ABSL_FLAG(double, stride, 1,
          "Defines each stride of the search. The larger the stride is, "
          "the coarser the result will be, but will also be faster.");
ABSL_FLAG(double, error_tolerance, 0.01,
          "Defines threshold for stopping the search. When current search "
          "range is narrower than the error_tolerance computed range, we "
          "stop the search.");

ABSL_FLAG(std::string, qps_server_target_override, "",
          "Override QPS server target to configure in client configs."
          "Only applicable if there is a single benchmark server.");

ABSL_FLAG(std::string, json_file_out, "", "File to write the JSON output to.");

ABSL_FLAG(std::string, credential_type, grpc::testing::kInsecureCredentialsType,
          "Credential type for communication with workers");
ABSL_FLAG(
    std::string, per_worker_credential_types, "",
    "A map of QPS worker addresses to credential types. When creating a "
    "channel to a QPS worker's driver port, the qps_json_driver first checks "
    "if the 'name:port' string is in the map, and it uses the corresponding "
    "credential type if so. If the QPS worker's 'name:port' string is not "
    "in the map, then the driver -> worker channel will be created with "
    "the credentials specified in --credential_type. The value of this flag "
    "is a semicolon-separated list of map entries, where each map entry is "
    "a comma-separated pair.");
ABSL_FLAG(bool, run_inproc, false, "Perform an in-process transport test");
ABSL_FLAG(
    int32_t, median_latency_collection_interval_millis, 0,
    "Specifies the period between gathering latency medians in "
    "milliseconds. The medians will be logged out on the client at the "
    "end of the benchmark run. If 0, this periodic collection is disabled.");

namespace grpc {
namespace testing {

static std::map<std::string, std::string>
ConstructPerWorkerCredentialTypesMap() {
  // Parse a list of the form: "addr1,cred_type1;addr2,cred_type2;..." into
  // a map.
  std::string remaining = absl::GetFlag(FLAGS_per_worker_credential_types);
  std::map<std::string, std::string> out;
  while (!remaining.empty()) {
    size_t next_semicolon = remaining.find(';');
    std::string next_entry = remaining.substr(0, next_semicolon);
    if (next_semicolon == std::string::npos) {
      remaining = "";
    } else {
      remaining = remaining.substr(next_semicolon + 1, std::string::npos);
    }
    size_t comma = next_entry.find(',');
    if (comma == std::string::npos) {
      gpr_log(GPR_ERROR,
              "Expectd --per_worker_credential_types to be a list "
              "of the form: 'addr1,cred_type1;addr2,cred_type2;...' "
              "into.");
      abort();
    }
    std::string addr = next_entry.substr(0, comma);
    std::string cred_type = next_entry.substr(comma + 1, std::string::npos);
    if (out.find(addr) != out.end()) {
      gpr_log(GPR_ERROR,
              "Found duplicate addr in per_worker_credential_types.");
      abort();
    }
    out[addr] = cred_type;
  }
  return out;
}

static std::unique_ptr<ScenarioResult> RunAndReport(
    const Scenario& scenario,
    const std::map<std::string, std::string>& per_worker_credential_types,
    bool* success) {
  std::cerr << "RUNNING SCENARIO: " << scenario.name() << "\n";
  auto result = RunScenario(
      scenario.client_config(), scenario.num_clients(),
      scenario.server_config(), scenario.num_servers(),
      scenario.warmup_seconds(), scenario.benchmark_seconds(),
      !absl::GetFlag(FLAGS_run_inproc) ? scenario.spawn_local_worker_count()
                                       : -2,
      absl::GetFlag(FLAGS_qps_server_target_override),
      absl::GetFlag(FLAGS_credential_type), per_worker_credential_types,
      absl::GetFlag(FLAGS_run_inproc),
      absl::GetFlag(FLAGS_median_latency_collection_interval_millis));

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

  if (!absl::GetFlag(FLAGS_json_file_out).empty()) {
    std::ofstream json_outfile;
    json_outfile.open(absl::GetFlag(FLAGS_json_file_out));
    json_outfile << "{\"qps\": " << result->summary().qps() << "}\n";
    json_outfile.close();
  }

  return result;
}

static double GetCpuLoad(
    Scenario* scenario, double offered_load,
    const std::map<std::string, std::string>& per_worker_credential_types,
    bool* success) {
  scenario->mutable_client_config()
      ->mutable_load_params()
      ->mutable_poisson()
      ->set_offered_load(offered_load);
  auto result = RunAndReport(*scenario, per_worker_credential_types, success);
  return result->summary().server_cpu_usage();
}

static double BinarySearch(
    Scenario* scenario, double targeted_cpu_load, double low, double high,
    const std::map<std::string, std::string>& per_worker_credential_types,
    bool* success) {
  while (low <= high * (1 - absl::GetFlag(FLAGS_error_tolerance))) {
    double mid = low + (high - low) / 2;
    double current_cpu_load =
        GetCpuLoad(scenario, mid, per_worker_credential_types, success);
    gpr_log(GPR_DEBUG, "Binary Search: current_offered_load %.0f", mid);
    if (!*success) {
      gpr_log(GPR_ERROR, "Client/Server Failure");
      break;
    }
    if (targeted_cpu_load <= current_cpu_load) {
      high = mid - absl::GetFlag(FLAGS_stride);
    } else {
      low = mid + absl::GetFlag(FLAGS_stride);
    }
  }

  return low;
}

static double SearchOfferedLoad(
    double initial_offered_load, double targeted_cpu_load, Scenario* scenario,
    const std::map<std::string, std::string>& per_worker_credential_types,
    bool* success) {
  std::cerr << "RUNNING SCENARIO: " << scenario->name() << "\n";
  double current_offered_load = initial_offered_load;
  double current_cpu_load = GetCpuLoad(scenario, current_offered_load,
                                       per_worker_credential_types, success);
  if (current_cpu_load > targeted_cpu_load) {
    gpr_log(GPR_ERROR, "Initial offered load too high");
    return -1;
  }

  while (*success && (current_cpu_load < targeted_cpu_load)) {
    current_offered_load *= 2;
    current_cpu_load = GetCpuLoad(scenario, current_offered_load,
                                  per_worker_credential_types, success);
    gpr_log(GPR_DEBUG, "Binary Search: current_offered_load  %.0f",
            current_offered_load);
  }

  double targeted_offered_load =
      BinarySearch(scenario, targeted_cpu_load, current_offered_load / 2,
                   current_offered_load, per_worker_credential_types, success);

  return targeted_offered_load;
}

static bool QpsDriver() {
  std::string json;

  bool scfile = (!absl::GetFlag(FLAGS_scenarios_file).empty());
  bool scjson = (!absl::GetFlag(FLAGS_scenarios_json).empty());
  if ((!scfile && !scjson && !absl::GetFlag(FLAGS_quit)) ||
      (scfile && (scjson || absl::GetFlag(FLAGS_quit))) ||
      (scjson && absl::GetFlag(FLAGS_quit))) {
    gpr_log(GPR_ERROR,
            "Exactly one of --scenarios_file, --scenarios_json, "
            "or --quit must be set");
    abort();
  }

  auto per_worker_credential_types = ConstructPerWorkerCredentialTypesMap();
  if (scfile) {
    // Read the json data from disk
    FILE* json_file = fopen(absl::GetFlag(FLAGS_scenarios_file).c_str(), "r");
    GPR_ASSERT(json_file != nullptr);
    fseek(json_file, 0, SEEK_END);
    long len = ftell(json_file);
    char* data = new char[len];
    fseek(json_file, 0, SEEK_SET);
    GPR_ASSERT(len == (long)fread(data, 1, len, json_file));
    fclose(json_file);
    json = std::string(data, data + len);
    delete[] data;
  } else if (scjson) {
    json = absl::GetFlag(FLAGS_scenarios_json).c_str();
  } else if (absl::GetFlag(FLAGS_quit)) {
    return RunQuit(absl::GetFlag(FLAGS_credential_type),
                   per_worker_credential_types);
  }

  // Parse into an array of scenarios
  Scenarios scenarios;
  ParseJson(json.c_str(), "grpc.testing.Scenarios", &scenarios);
  bool success = true;

  // Make sure that there is at least some valid scenario here
  GPR_ASSERT(scenarios.scenarios_size() > 0);

  for (int i = 0; i < scenarios.scenarios_size(); i++) {
    if (absl::GetFlag(FLAGS_search_param).empty()) {
      const Scenario& scenario = scenarios.scenarios(i);
      RunAndReport(scenario, per_worker_credential_types, &success);
    } else {
      if (absl::GetFlag(FLAGS_search_param) == "offered_load") {
        Scenario* scenario = scenarios.mutable_scenarios(i);
        double targeted_offered_load =
            SearchOfferedLoad(absl::GetFlag(FLAGS_initial_search_value),
                              absl::GetFlag(FLAGS_targeted_cpu_load), scenario,
                              per_worker_credential_types, &success);
        gpr_log(GPR_INFO, "targeted_offered_load %f", targeted_offered_load);
        GetCpuLoad(scenario, targeted_offered_load, per_worker_credential_types,
                   &success);
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
  grpc::testing::TestEnvironment env(argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);

  bool ok = grpc::testing::QpsDriver();

  return ok ? 0 : 1;
}
