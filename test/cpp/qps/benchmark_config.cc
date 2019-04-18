/*
 *
 * Copyright 2015 gRPC authors.
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

#include "test/cpp/qps/benchmark_config.h"
#include <gflags/gflags.h>
#include <grpc/support/log.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "test/cpp/util/test_credentials_provider.h"

DEFINE_bool(enable_log_reporter, true,
            "Enable reporting of benchmark results through GprLog");

DEFINE_string(scenario_result_file, "",
              "Write JSON benchmark report to the file specified.");

DEFINE_string(hashed_id, "", "Hash of the user id");

DEFINE_string(test_name, "", "Name of the test being executed");

DEFINE_string(sys_info, "", "System information");

DEFINE_string(server_address, "localhost:50052",
              "Address of the performance database server");

DEFINE_string(tag, "", "Optional tag for the test");

DEFINE_string(rpc_reporter_server_address, "",
              "Server address for rpc reporter to send results to");

DEFINE_bool(enable_rpc_reporter, false, "Enable use of RPC reporter");

DEFINE_string(
    rpc_reporter_credential_type, grpc::testing::kInsecureCredentialsType,
    "Credential type for communication to the QPS benchmark report server");

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

namespace grpc {
namespace testing {

static std::shared_ptr<Reporter> InitBenchmarkReporters() {
  auto* composite_reporter = new CompositeReporter;
  if (FLAGS_enable_log_reporter) {
    composite_reporter->add(
        std::unique_ptr<Reporter>(new GprLogReporter("LogReporter")));
  }
  if (FLAGS_scenario_result_file != "") {
    composite_reporter->add(std::unique_ptr<Reporter>(
        new JsonReporter("JsonReporter", FLAGS_scenario_result_file)));
  }
  if (FLAGS_enable_rpc_reporter) {
    ChannelArguments channel_args;
    std::shared_ptr<ChannelCredentials> channel_creds =
        testing::GetCredentialsProvider()->GetChannelCredentials(
            FLAGS_rpc_reporter_credential_type, &channel_args);
    GPR_ASSERT(!FLAGS_rpc_reporter_server_address.empty());
    composite_reporter->add(std::unique_ptr<Reporter>(new RpcReporter(
        "RpcReporter", grpc::CreateChannel(FLAGS_rpc_reporter_server_address,
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
