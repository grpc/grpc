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
#include <unordered_map>

#include "absl/flags/flag.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/ext/gcp_observability.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/crash.h"
#include "test/core/util/test_config.h"
#include "test/cpp/interop/client_helper.h"
#include "test/cpp/interop/interop_client.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(bool, use_alts, false,
          "Whether to use alts. Enable alts will disable tls.");
ABSL_FLAG(bool, use_tls, false, "Whether to use tls.");
ABSL_FLAG(std::string, custom_credentials_type, "",
          "User provided credentials type.");
ABSL_FLAG(bool, use_test_ca, false, "False to use SSL roots for google");
ABSL_FLAG(int32_t, server_port, 0, "Server port.");
ABSL_FLAG(std::string, server_host, "localhost", "Server host to connect to");
ABSL_FLAG(std::string, server_host_override, "",
          "Override the server host which is sent in HTTP header");
ABSL_FLAG(
    std::string, test_case, "large_unary",
    "Configure different test cases. Valid options are:\n\n"
    "all : all test cases;\n"

    // TODO(veblush): Replace the help message with the following full message
    // once Abseil fixes the flag-help compiler error on Windows. (b/171659833)
    //
    //"cancel_after_begin : cancel stream after starting it;\n"
    //"cancel_after_first_response: cancel on first response;\n"
    //"channel_soak: sends 'soak_iterations' rpcs, rebuilds channel each
    // time;\n" "client_compressed_streaming : compressed request streaming with
    //" "client_compressed_unary : single compressed request;\n"
    //"client_streaming : request streaming with single response;\n"
    //"compute_engine_creds: large_unary with compute engine auth;\n"
    //"custom_metadata: server will echo custom metadata;\n"
    //"empty_stream : bi-di stream with no request/response;\n"
    //"empty_unary : empty (zero bytes) request and response;\n"
    //"google_default_credentials: large unary using GDC;\n"
    //"half_duplex : half-duplex streaming;\n"
    //"jwt_token_creds: large_unary with JWT token auth;\n"
    //"large_unary : single request and (large) response;\n"
    //"long_lived_channel: sends large_unary rpcs over a long-lived channel;\n"
    //"oauth2_auth_token: raw oauth2 access token auth;\n"
    //"per_rpc_creds: raw oauth2 access token on a single rpc;\n"
    //"ping_pong : full-duplex streaming;\n"
    //"response streaming;\n"
    //"rpc_soak: 'sends soak_iterations' large_unary rpcs;\n"
    //"server_compressed_streaming : single request with compressed "
    //"server_compressed_unary : single compressed response;\n"
    //"server_streaming : single request with response streaming;\n"
    //"slow_consumer : single request with response streaming with "
    //"slow client consumer;\n"
    //"special_status_message: verify Unicode and whitespace in status
    // message;\n" "status_code_and_message: verify status code & message;\n"
    //"timeout_on_sleeping_server: deadline exceeds on stream;\n"
    //"unimplemented_method: client calls an unimplemented method;\n"
    //"unimplemented_service: client calls an unimplemented service;\n"
    //
);
ABSL_FLAG(int32_t, num_times, 1, "Number of times to run the test case");
ABSL_FLAG(std::string, default_service_account, "",
          "Email of GCE default service account");
ABSL_FLAG(std::string, service_account_key_file, "",
          "Path to service account json key file.");
ABSL_FLAG(std::string, oauth_scope, "", "Scope for OAuth tokens.");
ABSL_FLAG(bool, do_not_abort_on_transient_failures, false,
          "If set to 'true', abort() is not called in case of transient "
          "failures (i.e failures that are temporary and will likely go away "
          "on retrying; like a temporary connection failure) and an error "
          "message is printed instead. Note that this flag just controls "
          "whether abort() is called or not. It does not control whether the "
          "test is retried in case of transient failures (and currently the "
          "interop tests are not retried even if this flag is set to true)");
ABSL_FLAG(int32_t, soak_iterations, 1000,
          "The number of iterations to use for the two soak tests; rpc_soak "
          "and channel_soak.");
ABSL_FLAG(int32_t, soak_max_failures, 0,
          "The number of iterations in soak tests that are allowed to fail "
          "(either due to non-OK status code or exceeding the "
          "per-iteration max acceptable latency).");
ABSL_FLAG(int32_t, soak_per_iteration_max_acceptable_latency_ms, 0,
          "The number of milliseconds a single iteration in the two soak "
          "tests (rpc_soak and channel_soak) should take.");
ABSL_FLAG(int32_t, soak_overall_timeout_seconds, 0,
          "The overall number of seconds after which a soak test should "
          "stop and fail, if the desired number of iterations have not yet "
          "completed.");
ABSL_FLAG(int32_t, soak_min_time_ms_between_rpcs, 0,
          "The minimum time in milliseconds between consecutive RPCs in a "
          "soak test (rpc_soak or channel_soak), useful for limiting QPS");
ABSL_FLAG(
    int32_t, soak_request_size, 271828,
    "The request size in a soak RPC. "
    "The default value is set based on the interop large unary test case.");
ABSL_FLAG(
    int32_t, soak_response_size, 314159,
    "The response size in a soak RPC. "
    "The default value is set based on the interop large unary test case.");
ABSL_FLAG(int32_t, iteration_interval, 10,
          "The interval in seconds between rpcs. This is used by "
          "long_connection test");
ABSL_FLAG(std::string, additional_metadata, "",
          "Additional metadata to send in each request, as a "
          "semicolon-separated list of key:value pairs.");
ABSL_FLAG(
    bool, log_metadata_and_status, false,
    "If set to 'true', will print received initial and trailing metadata, "
    "grpc-status and error message to the console, in a stable format.");
ABSL_FLAG(bool, enable_observability, false,
          "Whether to enable GCP Observability");

using grpc::testing::CreateChannelForTestCase;
using grpc::testing::GetServiceAccountJsonKey;
using grpc::testing::UpdateActions;

namespace {

// Parse the contents of FLAGS_additional_metadata into a map. Allow
// alphanumeric characters and dashes in keys, and any character but semicolons
// in values. Convert keys to lowercase. On failure, log an error and return
// false.
bool ParseAdditionalMetadataFlag(
    const std::string& flag,
    std::multimap<std::string, std::string>* additional_metadata) {
  size_t start_pos = 0;
  while (start_pos < flag.length()) {
    size_t colon_pos = flag.find(':', start_pos);
    if (colon_pos == std::string::npos) {
      gpr_log(GPR_ERROR,
              "Couldn't parse metadata flag: extra characters at end of flag");
      return false;
    }
    size_t semicolon_pos = flag.find(';', colon_pos);

    std::string key = flag.substr(start_pos, colon_pos - start_pos);
    std::string value =
        flag.substr(colon_pos + 1, semicolon_pos - colon_pos - 1);

    constexpr char alphanum_and_hyphen[] =
        "-0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (key.find_first_not_of(alphanum_and_hyphen) != std::string::npos) {
      gpr_log(GPR_ERROR,
              "Couldn't parse metadata flag: key contains characters other "
              "than alphanumeric and hyphens: %s",
              key.c_str());
      return false;
    }

    // Convert to lowercase.
    for (char& c : key) {
      if (c >= 'A' && c <= 'Z') {
        c += ('a' - 'A');
      }
    }

    gpr_log(GPR_INFO, "Adding additional metadata with key %s and value %s",
            key.c_str(), value.c_str());
    additional_metadata->insert({key, value});

    if (semicolon_pos == std::string::npos) {
      break;
    } else {
      start_pos = semicolon_pos + 1;
    }
  }

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  gpr_log(GPR_INFO, "Testing these cases: %s",
          absl::GetFlag(FLAGS_test_case).c_str());
  int ret = 0;

  if (absl::GetFlag(FLAGS_enable_observability)) {
    auto status = grpc::experimental::GcpObservabilityInit();
    gpr_log(GPR_DEBUG, "GcpObservabilityInit() status_code: %d", status.code());
    if (!status.ok()) {
      return 1;
    }
  }

  grpc::testing::ChannelCreationFunc channel_creation_func;
  std::string test_case = absl::GetFlag(FLAGS_test_case);
  if (absl::GetFlag(FLAGS_additional_metadata).empty()) {
    channel_creation_func = [test_case](auto arguments) {
      std::vector<std::unique_ptr<
          grpc::experimental::ClientInterceptorFactoryInterface>>
          factories;
      if (absl::GetFlag(FLAGS_log_metadata_and_status)) {
        factories.emplace_back(
            new grpc::testing::MetadataAndStatusLoggerInterceptorFactory());
      }
      return CreateChannelForTestCase(test_case, std::move(factories),
                                      arguments);
    };
  } else {
    std::multimap<std::string, std::string> additional_metadata;
    if (!ParseAdditionalMetadataFlag(absl::GetFlag(FLAGS_additional_metadata),
                                     &additional_metadata)) {
      return 1;
    }

    channel_creation_func = [test_case, additional_metadata](auto arguments) {
      std::vector<std::unique_ptr<
          grpc::experimental::ClientInterceptorFactoryInterface>>
          factories;
      factories.emplace_back(
          new grpc::testing::AdditionalMetadataInterceptorFactory(
              additional_metadata));
      if (absl::GetFlag(FLAGS_log_metadata_and_status)) {
        factories.emplace_back(
            new grpc::testing::MetadataAndStatusLoggerInterceptorFactory());
      }
      return CreateChannelForTestCase(test_case, std::move(factories),
                                      arguments);
    };
  }

  grpc::testing::InteropClient client(
      channel_creation_func, true,
      absl::GetFlag(FLAGS_do_not_abort_on_transient_failures));

  std::unordered_map<std::string, std::function<bool()>> actions;
  actions["empty_unary"] =
      std::bind(&grpc::testing::InteropClient::DoEmpty, &client);
  actions["large_unary"] =
      std::bind(&grpc::testing::InteropClient::DoLargeUnary, &client);
  actions["server_compressed_unary"] = std::bind(
      &grpc::testing::InteropClient::DoServerCompressedUnary, &client);
  actions["client_compressed_unary"] = std::bind(
      &grpc::testing::InteropClient::DoClientCompressedUnary, &client);
  actions["client_streaming"] =
      std::bind(&grpc::testing::InteropClient::DoRequestStreaming, &client);
  actions["server_streaming"] =
      std::bind(&grpc::testing::InteropClient::DoResponseStreaming, &client);
  actions["server_compressed_streaming"] = std::bind(
      &grpc::testing::InteropClient::DoServerCompressedStreaming, &client);
  actions["client_compressed_streaming"] = std::bind(
      &grpc::testing::InteropClient::DoClientCompressedStreaming, &client);
  actions["slow_consumer"] = std::bind(
      &grpc::testing::InteropClient::DoResponseStreamingWithSlowConsumer,
      &client);
  actions["half_duplex"] =
      std::bind(&grpc::testing::InteropClient::DoHalfDuplex, &client);
  actions["ping_pong"] =
      std::bind(&grpc::testing::InteropClient::DoPingPong, &client);
  actions["cancel_after_begin"] =
      std::bind(&grpc::testing::InteropClient::DoCancelAfterBegin, &client);
  actions["cancel_after_first_response"] = std::bind(
      &grpc::testing::InteropClient::DoCancelAfterFirstResponse, &client);
  actions["timeout_on_sleeping_server"] = std::bind(
      &grpc::testing::InteropClient::DoTimeoutOnSleepingServer, &client);
  actions["empty_stream"] =
      std::bind(&grpc::testing::InteropClient::DoEmptyStream, &client);
  actions["pick_first_unary"] =
      std::bind(&grpc::testing::InteropClient::DoPickFirstUnary, &client);
  if (absl::GetFlag(FLAGS_use_tls)) {
    actions["compute_engine_creds"] =
        std::bind(&grpc::testing::InteropClient::DoComputeEngineCreds, &client,
                  absl::GetFlag(FLAGS_default_service_account),
                  absl::GetFlag(FLAGS_oauth_scope));
    actions["jwt_token_creds"] =
        std::bind(&grpc::testing::InteropClient::DoJwtTokenCreds, &client,
                  GetServiceAccountJsonKey());
    actions["oauth2_auth_token"] =
        std::bind(&grpc::testing::InteropClient::DoOauth2AuthToken, &client,
                  absl::GetFlag(FLAGS_default_service_account),
                  absl::GetFlag(FLAGS_oauth_scope));
    actions["per_rpc_creds"] =
        std::bind(&grpc::testing::InteropClient::DoPerRpcCreds, &client,
                  GetServiceAccountJsonKey());
  }
  if (absl::GetFlag(FLAGS_custom_credentials_type) ==
      "google_default_credentials") {
    actions["google_default_credentials"] =
        std::bind(&grpc::testing::InteropClient::DoGoogleDefaultCredentials,
                  &client, absl::GetFlag(FLAGS_default_service_account));
  }
  actions["status_code_and_message"] =
      std::bind(&grpc::testing::InteropClient::DoStatusWithMessage, &client);
  actions["special_status_message"] =
      std::bind(&grpc::testing::InteropClient::DoSpecialStatusMessage, &client);
  actions["custom_metadata"] =
      std::bind(&grpc::testing::InteropClient::DoCustomMetadata, &client);
  actions["unimplemented_method"] =
      std::bind(&grpc::testing::InteropClient::DoUnimplementedMethod, &client);
  actions["unimplemented_service"] =
      std::bind(&grpc::testing::InteropClient::DoUnimplementedService, &client);
  actions["channel_soak"] = std::bind(
      &grpc::testing::InteropClient::DoChannelSoakTest, &client,
      absl::GetFlag(FLAGS_server_host), absl::GetFlag(FLAGS_soak_iterations),
      absl::GetFlag(FLAGS_soak_max_failures),
      absl::GetFlag(FLAGS_soak_per_iteration_max_acceptable_latency_ms),
      absl::GetFlag(FLAGS_soak_min_time_ms_between_rpcs),
      absl::GetFlag(FLAGS_soak_overall_timeout_seconds),
      absl::GetFlag(FLAGS_soak_request_size),
      absl::GetFlag(FLAGS_soak_response_size));
  actions["rpc_soak"] = std::bind(
      &grpc::testing::InteropClient::DoRpcSoakTest, &client,
      absl::GetFlag(FLAGS_server_host), absl::GetFlag(FLAGS_soak_iterations),
      absl::GetFlag(FLAGS_soak_max_failures),
      absl::GetFlag(FLAGS_soak_per_iteration_max_acceptable_latency_ms),
      absl::GetFlag(FLAGS_soak_min_time_ms_between_rpcs),
      absl::GetFlag(FLAGS_soak_overall_timeout_seconds),
      absl::GetFlag(FLAGS_soak_request_size),
      absl::GetFlag(FLAGS_soak_response_size));
  actions["long_lived_channel"] =
      std::bind(&grpc::testing::InteropClient::DoLongLivedChannelTest, &client,
                absl::GetFlag(FLAGS_soak_iterations),
                absl::GetFlag(FLAGS_iteration_interval));

  UpdateActions(&actions);

  if (absl::GetFlag(FLAGS_test_case) == "all") {
    for (const auto& action : actions) {
      for (int i = 0; i < absl::GetFlag(FLAGS_num_times); i++) {
        action.second();
      }
    }
  } else if (actions.find(absl::GetFlag(FLAGS_test_case)) != actions.end()) {
    for (int i = 0; i < absl::GetFlag(FLAGS_num_times); i++) {
      actions.find(absl::GetFlag(FLAGS_test_case))->second();
    }
  } else {
    std::string test_cases;
    for (const auto& action : actions) {
      if (!test_cases.empty()) test_cases += "\n";
      test_cases += action.first;
    }
    gpr_log(GPR_ERROR, "Unsupported test case %s. Valid options are\n%s",
            absl::GetFlag(FLAGS_test_case).c_str(), test_cases.c_str());
    ret = 1;
  }

  if (absl::GetFlag(FLAGS_enable_observability)) {
    grpc::experimental::GcpObservabilityClose();
  }

  return ret;
}
