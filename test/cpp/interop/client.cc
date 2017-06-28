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

#include <memory>
#include <unordered_map>

#include <gflags/gflags.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/support/string.h"
#include "test/cpp/interop/client_helper.h"
#include "test/cpp/interop/interop_client.h"
#include "test/cpp/util/test_config.h"

DEFINE_bool(use_tls, false, "Whether to use tls.");
DEFINE_string(custom_credentials_type, "", "User provided credentials type.");
DEFINE_bool(use_test_ca, false, "False to use SSL roots for google");
DEFINE_int32(server_port, 0, "Server port.");
DEFINE_string(server_host, "localhost", "Server host to connect to");
DEFINE_string(server_host_override, "foo.test.google.fr",
              "Override the server host which is sent in HTTP header");
DEFINE_string(
    test_case, "large_unary",
    "Configure different test cases. Valid options are:\n\n"
    "all : all test cases;\n"
    "cancel_after_begin : cancel stream after starting it;\n"
    "cancel_after_first_response: cancel on first response;\n"
    "client_compressed_streaming : compressed request streaming with "
    "client_compressed_unary : single compressed request;\n"
    "client_streaming : request streaming with single response;\n"
    "compute_engine_creds: large_unary with compute engine auth;\n"
    "custom_metadata: server will echo custom metadata;\n"
    "empty_stream : bi-di stream with no request/response;\n"
    "empty_unary : empty (zero bytes) request and response;\n"
    "half_duplex : half-duplex streaming;\n"
    "jwt_token_creds: large_unary with JWT token auth;\n"
    "large_unary : single request and (large) response;\n"
    "oauth2_auth_token: raw oauth2 access token auth;\n"
    "per_rpc_creds: raw oauth2 access token on a single rpc;\n"
    "ping_pong : full-duplex streaming;\n"
    "response streaming;\n"
    "server_compressed_streaming : single request with compressed "
    "server_compressed_unary : single compressed response;\n"
    "server_streaming : single request with response streaming;\n"
    "slow_consumer : single request with response streaming with "
    "slow client consumer;\n"
    "status_code_and_message: verify status code & message;\n"
    "timeout_on_sleeping_server: deadline exceeds on stream;\n"
    "unimplemented_method: client calls an unimplemented method;\n"
    "unimplemented_service: client calls an unimplemented service;\n");
DEFINE_string(default_service_account, "",
              "Email of GCE default service account");
DEFINE_string(service_account_key_file, "",
              "Path to service account json key file.");
DEFINE_string(oauth_scope, "", "Scope for OAuth tokens.");
DEFINE_bool(do_not_abort_on_transient_failures, false,
            "If set to 'true', abort() is not called in case of transient "
            "failures (i.e failures that are temporary and will likely go away "
            "on retrying; like a temporary connection failure) and an error "
            "message is printed instead. Note that this flag just controls "
            "whether abort() is called or not. It does not control whether the "
            "test is retried in case of transient failures (and currently the "
            "interop tests are not retried even if this flag is set to true)");

using grpc::testing::CreateChannelForTestCase;
using grpc::testing::GetServiceAccountJsonKey;
using grpc::testing::UpdateActions;

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  gpr_log(GPR_INFO, "Testing these cases: %s", FLAGS_test_case.c_str());
  int ret = 0;
  grpc::testing::InteropClient client(CreateChannelForTestCase(FLAGS_test_case),
                                      true,
                                      FLAGS_do_not_abort_on_transient_failures);

  std::unordered_map<grpc::string, std::function<bool()>> actions;
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
  if (FLAGS_use_tls) {
    actions["compute_engine_creds"] =
        std::bind(&grpc::testing::InteropClient::DoComputeEngineCreds, &client,
                  FLAGS_default_service_account, FLAGS_oauth_scope);
    actions["jwt_token_creds"] =
        std::bind(&grpc::testing::InteropClient::DoJwtTokenCreds, &client,
                  GetServiceAccountJsonKey());
    actions["oauth2_auth_token"] =
        std::bind(&grpc::testing::InteropClient::DoOauth2AuthToken, &client,
                  FLAGS_default_service_account, FLAGS_oauth_scope);
    actions["per_rpc_creds"] =
        std::bind(&grpc::testing::InteropClient::DoPerRpcCreds, &client,
                  GetServiceAccountJsonKey());
  }
  actions["status_code_and_message"] =
      std::bind(&grpc::testing::InteropClient::DoStatusWithMessage, &client);
  actions["custom_metadata"] =
      std::bind(&grpc::testing::InteropClient::DoCustomMetadata, &client);
  actions["unimplemented_method"] =
      std::bind(&grpc::testing::InteropClient::DoUnimplementedMethod, &client);
  actions["unimplemented_service"] =
      std::bind(&grpc::testing::InteropClient::DoUnimplementedService, &client);
  actions["cacheable_unary"] =
      std::bind(&grpc::testing::InteropClient::DoCacheableUnary, &client);

  UpdateActions(&actions);

  if (FLAGS_test_case == "all") {
    for (const auto& action : actions) {
      action.second();
    }
  } else if (actions.find(FLAGS_test_case) != actions.end()) {
    actions.find(FLAGS_test_case)->second();
  } else {
    grpc::string test_cases;
    for (const auto& action : actions) {
      if (!test_cases.empty()) test_cases += "\n";
      test_cases += action.first;
    }
    gpr_log(GPR_ERROR, "Unsupported test case %s. Valid options are\n%s",
            FLAGS_test_case.c_str(), test_cases.c_str());
    ret = 1;
  }

  return ret;
}
