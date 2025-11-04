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

#ifndef GRPC_TEST_CPP_QPS_DRIVER_H
#define GRPC_TEST_CPP_QPS_DRIVER_H

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "src/proto/grpc/testing/control.pb.h"
#include "test/cpp/qps/histogram.h"
#include "test/cpp/util/test_credentials_provider.h"  // For kInsecureCredentialsType

namespace grpc {
namespace testing {

class RunScenarioOptions {
 public:
  // Required parameters, passed to constructor
  const grpc::testing::ClientConfig& client_config;
  const grpc::testing::ServerConfig& server_config;

  // Optional parameters with defaults
  size_t num_clients = 1;
  size_t num_servers = 1;
  int warmup_seconds = 1;
  int benchmark_seconds = 3;
  int spawn_local_worker_count = -2;
  std::string qps_server_target_override;
  std::string credential_type = grpc::testing::kInsecureCredentialsType;
  std::map<std::string, std::string> per_worker_credential_types = {};
  bool run_inproc = false;
  int32_t median_latency_collection_interval_millis = 0;
  std::optional<std::string> latent_see_directory = std::nullopt;

  RunScenarioOptions(const grpc::testing::ClientConfig& client_cfg,
                     const grpc::testing::ServerConfig& server_cfg)
      : client_config(client_cfg), server_config(server_cfg) {}

  RunScenarioOptions& set_num_clients(size_t val) {
    num_clients = val;
    return *this;
  }
  RunScenarioOptions& set_num_servers(size_t val) {
    num_servers = val;
    return *this;
  }
  RunScenarioOptions& set_warmup_seconds(int val) {
    warmup_seconds = val;
    return *this;
  }
  RunScenarioOptions& set_benchmark_seconds(int val) {
    benchmark_seconds = val;
    return *this;
  }
  RunScenarioOptions& set_spawn_local_worker_count(int val) {
    spawn_local_worker_count = val;
    return *this;
  }
  RunScenarioOptions& set_qps_server_target_override(const std::string& val) {
    qps_server_target_override = val;
    return *this;
  }
  RunScenarioOptions& set_credential_type(const std::string& val) {
    credential_type = val;
    return *this;
  }
  RunScenarioOptions& set_per_worker_credential_types(
      const std::map<std::string, std::string>& val) {
    per_worker_credential_types = val;
    return *this;
  }
  RunScenarioOptions& set_run_inproc(bool val) {
    run_inproc = val;
    return *this;
  }
  RunScenarioOptions& set_median_latency_collection_interval_millis(
      int32_t val) {
    median_latency_collection_interval_millis = val;
    return *this;
  }
  RunScenarioOptions& set_latent_see_directory(
      const std::optional<std::string>& val) {
    latent_see_directory = val;
    return *this;
  }
};

std::unique_ptr<ScenarioResult> RunScenario(const RunScenarioOptions& options);

bool RunQuit(
    const std::string& credential_type,
    const std::map<std::string, std::string>& per_worker_credential_types);
}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_QPS_DRIVER_H
