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

#include "test/cpp/qps/benchmark_config.h"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "src/core/util/crash.h"
#include "test/cpp/util/test_credentials_provider.h"

ABSL_FLAG(bool, enable_log_reporter, true,
          "Enable reporting of benchmark results through GprLog");

ABSL_FLAG(std::string, scenario_result_file, "",
          "Write JSON benchmark report to the file specified.");

ABSL_FLAG(std::string, hashed_id, "", "Hash of the user id");

ABSL_FLAG(std::string, test_name, "", "Name of the test being executed");

ABSL_FLAG(std::string, sys_info, "", "System information");

ABSL_FLAG(std::string, server_address, "localhost:50052",
          "Address of the performance database server");

ABSL_FLAG(std::string, tag, "", "Optional tag for the test");

ABSL_FLAG(std::string, rpc_reporter_server_address, "",
          "Server address for rpc reporter to send results to");

ABSL_FLAG(bool, enable_rpc_reporter, false, "Enable use of RPC reporter");

ABSL_FLAG(
    std::string, rpc_reporter_credential_type,
    grpc::testing::kInsecureCredentialsType,
    "Credential type for communication to the QPS benchmark report server");

namespace grpc {
namespace testing {

static std::shared_ptr<Reporter> InitBenchmarkReporters() {
  auto* composite_reporter = new CompositeReporter;
  if (absl::GetFlag(FLAGS_enable_log_reporter)) {
    composite_reporter->add(
        std::unique_ptr<Reporter>(new GprLogReporter("LogReporter")));
  }
  if (!absl::GetFlag(FLAGS_scenario_result_file).empty()) {
    composite_reporter->add(std::unique_ptr<Reporter>(new JsonReporter(
        "JsonReporter", absl::GetFlag(FLAGS_scenario_result_file))));
  }
  if (absl::GetFlag(FLAGS_enable_rpc_reporter)) {
    ChannelArguments channel_args;
    std::shared_ptr<ChannelCredentials> channel_creds =
        testing::GetCredentialsProvider()->GetChannelCredentials(
            absl::GetFlag(FLAGS_rpc_reporter_credential_type), &channel_args);
    CHECK(!absl::GetFlag(FLAGS_rpc_reporter_server_address).empty());
    composite_reporter->add(std::unique_ptr<Reporter>(new RpcReporter(
        "RpcReporter",
        grpc::CreateChannel(absl::GetFlag(FLAGS_rpc_reporter_server_address),
                            channel_creds))));
  }

  return std::shared_ptr<Reporter>(composite_reporter);
}

std::shared_ptr<Reporter> GetReporter() {
  static std::shared_ptr<Reporter> reporter(InitBenchmarkReporters());
  return reporter;
}

}  // namespace testing
}  // namespace grpc
