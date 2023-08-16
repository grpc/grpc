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

#include <memory>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/strings/str_split.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/string.h"
#include "test/core/util/test_config.h"
#include "test/cpp/interop/client_helper.h"
#include "test/cpp/interop/interop_client.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(std::string, server_uris, "",
          "Comma-separated list of sever URIs to make RPCs to");
ABSL_FLAG(std::string, credentials_types, "",
          "Comma-separated list of credentials, each entry is used for the "
          "server of the corresponding index in server_uris. Supported values: "
          "compute_engine_channel_creds, INSECURE_CREDENTIALS");
ABSL_FLAG(int32_t, soak_iterations, 10,
          "The number of iterations to use for the two soak tests: rpc_soak "
          "and channel_soak");
ABSL_FLAG(int32_t, soak_max_failures, 0,
          "The number of iterations in soak tests that are allowed to fail "
          "(either due to non-OK status code or exceeding the per-iteration "
          "max acceptable latency).");
ABSL_FLAG(int32_t, soak_per_iteration_max_acceptable_latency_ms, 1000,
          "The number of milliseconds a single iteration in the two soak tests "
          "(rpc_soak and channel_soak) should take.");
ABSL_FLAG(
    int32_t, soak_overall_timeout_seconds, 10,
    "The overall number of seconds after which a soak test should stop and "
    "fail, if the desired number of iterations have not yet completed.");
ABSL_FLAG(int32_t, soak_min_time_ms_between_rpcs, 0,
          "The minimum time in milliseconds between consecutive RPCs in a soak "
          "test (rpc_soak or channel_soak), useful for limiting QPS");
ABSL_FLAG(
    int32_t, soak_request_size, 271828,
    "The request size in a soak RPC. "
    "The default value is set based on the interop large unary test case.");
ABSL_FLAG(
    int32_t, soak_response_size, 314159,
    "The response size in a soak RPCi. "
    "The default value is set based on the interop large unary test case.");
ABSL_FLAG(std::string, test_case, "rpc_soak",
          "Configure different test cases. Valid options are: "
          "rpc_soak: sends --soak_iterations large_unary RPCs; "
          "channel_soak: sends --soak_iterations RPCs, rebuilding the channel "
          "each time");

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  gpr_log(GPR_INFO, "Testing these cases: %s",
          absl::GetFlag(FLAGS_test_case).c_str());
  std::string test_case = absl::GetFlag(FLAGS_test_case);
  // validate flags
  std::vector<std::string> uris =
      absl::StrSplit(absl::GetFlag(FLAGS_server_uris), ',');
  std::vector<std::string> creds =
      absl::StrSplit(absl::GetFlag(FLAGS_credentials_types), ',');
  if (uris.size() != creds.size()) {
    gpr_log(GPR_ERROR,
            "Number of entries in --server_uris %ld != number of entries in "
            "--credentials_types %ld",
            uris.size(), creds.size());
    GPR_ASSERT(0);
  }
  if (uris.empty()) {
    gpr_log(GPR_ERROR, "--server_uris has zero entries");
    GPR_ASSERT(0);
  }
  // construct and start clients
  std::vector<std::thread> threads;
  for (size_t i = 0; i < uris.size(); i++) {
    threads.push_back(std::thread([uris, creds, i, test_case]() {
      auto channel_creation_func = [uris, creds, i](grpc::ChannelArguments) {
        return grpc::CreateTestChannel(uris[i], creds[i],
                                       nullptr /* call creds */);
      };
      grpc::testing::InteropClient client(channel_creation_func, true, false);
      if (test_case == "rpc_soak") {
        client.DoRpcSoakTest(
            uris[i], absl::GetFlag(FLAGS_soak_iterations),
            absl::GetFlag(FLAGS_soak_max_failures),
            absl::GetFlag(FLAGS_soak_per_iteration_max_acceptable_latency_ms),
            absl::GetFlag(FLAGS_soak_min_time_ms_between_rpcs),
            absl::GetFlag(FLAGS_soak_overall_timeout_seconds),
            absl::GetFlag(FLAGS_soak_request_size),
            absl::GetFlag(FLAGS_soak_response_size));
      } else if (test_case == "channel_soak") {
        client.DoChannelSoakTest(
            uris[i], absl::GetFlag(FLAGS_soak_iterations),
            absl::GetFlag(FLAGS_soak_max_failures),
            absl::GetFlag(FLAGS_soak_per_iteration_max_acceptable_latency_ms),
            absl::GetFlag(FLAGS_soak_min_time_ms_between_rpcs),
            absl::GetFlag(FLAGS_soak_overall_timeout_seconds),
            absl::GetFlag(FLAGS_soak_request_size),
            absl::GetFlag(FLAGS_soak_response_size));
      } else {
        gpr_log(GPR_ERROR,
                "Invalid test case, must be either rpc_soak or channel_soak");
        GPR_ASSERT(0);
      }
    }));
  }
  for (auto& thd : threads) {
    thd.join();
  }
  gpr_log(GPR_INFO, "All clients done!");
  return 0;
}
