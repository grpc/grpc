/*
 *
 * Copyright 2015-2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

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

namespace grpc {
namespace testing {

static std::unique_ptr<ScenarioResult> RunAndReport(const Scenario& scenario,
                                                    bool* success) {
  std::cerr << "RUNNING SCENARIO: " << scenario.name() << "\n";
  auto result =
      RunScenario(scenario.client_config(), scenario.num_clients(),
                  scenario.server_config(), scenario.num_servers(),
                  scenario.warmup_seconds(), scenario.benchmark_seconds(),
                  scenario.spawn_local_worker_count(),
                  FLAGS_qps_server_target_override.c_str());

  // Amend the result with scenario config. Eventually we should adjust
  // RunScenario contract so we don't need to touch the result here.
  result->mutable_scenario()->CopyFrom(scenario);

  GetReporter()->ReportQPS(*result);
  GetReporter()->ReportQPSPerCore(*result);
  GetReporter()->ReportLatency(*result);
  GetReporter()->ReportTimes(*result);
  GetReporter()->ReportCpuUsage(*result);

  for (int i = 0; *success && i < result->client_success_size(); i++) {
    *success = result->client_success(i);
  }
  for (int i = 0; *success && i < result->server_success_size(); i++) {
    *success = result->server_success(i);
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
    GPR_ASSERT(json_file != NULL);
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
    return RunQuit();
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
  grpc::testing::InitBenchmark(&argc, &argv, true);

  bool ok = grpc::testing::QpsDriver();

  return ok ? 0 : 1;
}
