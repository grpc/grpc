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

#include <memory>
#include <set>

#include <grpc++/impl/codegen/config_protobuf.h>

#include <gflags/gflags.h>
#include <grpc/support/log.h>

#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/parse_json.h"
#include "test/cpp/qps/report.h"
#include "test/cpp/util/benchmark_config.h"

DEFINE_string(scenarios_file, "",
              "JSON file containing an array of Scenario objects");
DEFINE_string(scenarios_json, "",
              "JSON string containing an array of Scenario objects");
DEFINE_bool(quit, false, "Quit the workers");

namespace grpc {
namespace testing {

static void QpsDriver() {
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
    FILE *json_file = fopen(FLAGS_scenarios_file.c_str(), "r");
    GPR_ASSERT(json_file != NULL);
    fseek(json_file, 0, SEEK_END);
    long len = ftell(json_file);
    char *data = new char[len];
    fseek(json_file, 0, SEEK_SET);
    GPR_ASSERT(len == (long)fread(data, 1, len, json_file));
    fclose(json_file);
    json = grpc::string(data, data + len);
    delete[] data;
  } else if (scjson) {
    json = FLAGS_scenarios_json.c_str();
  } else if (FLAGS_quit) {
    RunQuit();
    return;
  }

  // Parse into an array of scenarios
  Scenarios scenarios;
  ParseJson(json.c_str(), "grpc.testing.Scenarios", &scenarios);

  // Make sure that there is at least some valid scenario here
  GPR_ASSERT(scenarios.scenarios_size() > 0);

  for (int i = 0; i < scenarios.scenarios_size(); i++) {
    const Scenario &scenario = scenarios.scenarios(i);
    std::cerr << "RUNNING SCENARIO: " << scenario.name() << "\n";
    auto result =
        RunScenario(scenario.client_config(), scenario.num_clients(),
                    scenario.server_config(), scenario.num_servers(),
                    scenario.warmup_seconds(), scenario.benchmark_seconds(),
                    scenario.spawn_local_worker_count());

    // Amend the result with scenario config. Eventually we should adjust
    // RunScenario contract so we don't need to touch the result here.
    result->mutable_scenario()->CopyFrom(scenario);

    GetReporter()->ReportQPS(*result);
    GetReporter()->ReportQPSPerCore(*result);
    GetReporter()->ReportLatency(*result);
    GetReporter()->ReportTimes(*result);
  }
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char **argv) {
  grpc::testing::InitBenchmark(&argc, &argv, true);

  grpc::testing::QpsDriver();

  return 0;
}
