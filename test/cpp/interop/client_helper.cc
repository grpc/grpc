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

#include "test/cpp/interop/client_helper.h"

#include <fstream>
#include <memory>
#include <sstream>

#include "absl/flags/flag.h"
#include "absl/strings/match.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/slice/b64.h"
#include "src/cpp/client/secure_credentials.h"
#include "test/core/security/oauth2_utils.h"
#include "test/cpp/interop/client_flags.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/util/test_credentials_provider.h"

namespace grpc {
namespace testing {

std::string GetServiceAccountJsonKey() {
  static std::string json_key;
  if (json_key.empty()) {
    std::ifstream json_key_file(absl::GetFlag(FLAGS_service_account_key_file));
    std::stringstream key_stream;
    key_stream << json_key_file.rdbuf();
    json_key = key_stream.str();
  }
  return json_key;
}

std::string GetOauth2AccessToken() {
  std::shared_ptr<CallCredentials> creds = GoogleComputeEngineCredentials();
  SecureCallCredentials* secure_creds =
      dynamic_cast<SecureCallCredentials*>(creds.get());
  GPR_ASSERT(secure_creds != nullptr);
  grpc_call_credentials* c_creds = secure_creds->GetRawCreds();
  char* token = grpc_test_fetch_oauth2_token_with_credentials(c_creds);
  GPR_ASSERT(token != nullptr);
  gpr_log(GPR_INFO, "Get raw oauth2 access token: %s", token);
  std::string access_token(token + sizeof("Bearer ") - 1);
  gpr_free(token);
  return access_token;
}

void UpdateActions(
    std::unordered_map<std::string, std::function<bool()>>* /*actions*/) {}

std::shared_ptr<Channel> CreateChannelForTestCase(
    const std::string& test_case,
    std::vector<
        std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
        interceptor_creators) {
  std::string server_uri = absl::GetFlag(FLAGS_server_host);
  int32_t port = absl::GetFlag(FLAGS_server_port);
  if (port != 0) {
    absl::StrAppend(&server_uri, ":", std::to_string(port));
  }
  std::shared_ptr<CallCredentials> creds;
  if (test_case == "compute_engine_creds") {
    creds = absl::GetFlag(FLAGS_custom_credentials_type) ==
                    "google_default_credentials"
                ? nullptr
                : GoogleComputeEngineCredentials();
  } else if (test_case == "jwt_token_creds") {
    std::string json_key = GetServiceAccountJsonKey();
    std::chrono::seconds token_lifetime = std::chrono::hours(1);
    creds = absl::GetFlag(FLAGS_custom_credentials_type) ==
                    "google_default_credentials"
                ? nullptr
                : ServiceAccountJWTAccessCredentials(json_key,
                                                     token_lifetime.count());
  } else if (test_case == "oauth2_auth_token") {
    creds = absl::GetFlag(FLAGS_custom_credentials_type) ==
                    "google_default_credentials"
                ? nullptr
                : AccessTokenCredentials(GetOauth2AccessToken());
  } else if (test_case == "pick_first_unary") {
    ChannelArguments channel_args;
    // allow the LB policy to be configured with service config
    channel_args.SetInt(GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION, 0);
    return CreateTestChannel(
        server_uri, absl::GetFlag(FLAGS_custom_credentials_type),
        absl::GetFlag(FLAGS_server_host_override),
        !absl::GetFlag(FLAGS_use_test_ca), creds, channel_args);
  }
  if (absl::GetFlag(FLAGS_custom_credentials_type).empty()) {
    transport_security security_type =
        absl::GetFlag(FLAGS_use_alts)
            ? ALTS
            : (absl::GetFlag(FLAGS_use_tls) ? TLS : INSECURE);
    return CreateTestChannel(server_uri,
                             absl::GetFlag(FLAGS_server_host_override),
                             security_type, !absl::GetFlag(FLAGS_use_test_ca),
                             creds, std::move(interceptor_creators));
  } else {
    if (interceptor_creators.empty()) {
      return CreateTestChannel(
          server_uri, absl::GetFlag(FLAGS_custom_credentials_type), creds);
    } else {
      return CreateTestChannel(server_uri,
                               absl::GetFlag(FLAGS_custom_credentials_type),
                               creds, std::move(interceptor_creators));
    }
  }
}

static void log_metadata_entry(const std::string& prefix,
                               const grpc::string_ref& key,
                               const grpc::string_ref& value) {
  auto key_str = std::string(key.begin(), key.end());
  auto value_str = std::string(value.begin(), value.end());
  if (absl::EndsWith(key_str, "-bin")) {
    auto converted =
        grpc_base64_encode(value_str.c_str(), value_str.length(), 0, 0);
    value_str = std::string(converted);
    gpr_free(converted);
  }
  gpr_log(GPR_ERROR, "%s %s: %s", prefix.c_str(), key_str.c_str(),
          value_str.c_str());
}

void MetadataAndStatusLoggerInterceptor::Intercept(
    experimental::InterceptorBatchMethods* methods) {
  if (methods->QueryInterceptionHookPoint(
          experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {
    auto initial_metadata = methods->GetRecvInitialMetadata();

    for (const auto& entry : *initial_metadata) {
      log_metadata_entry("GRPC_INITIAL_METADATA", entry.first, entry.second);
    }
  }

  if (methods->QueryInterceptionHookPoint(
          experimental::InterceptionHookPoints::POST_RECV_STATUS)) {
    auto trailing_metadata = methods->GetRecvTrailingMetadata();
    for (const auto& entry : *trailing_metadata) {
      log_metadata_entry("GRPC_TRAILING_METADATA", entry.first, entry.second);
    }

    auto status = methods->GetRecvStatus();
    gpr_log(GPR_ERROR, "GRPC_STATUS %d", status->error_code());
    gpr_log(GPR_ERROR, "GRPC_ERROR_MESSAGE %s",
            status->error_message().c_str());
  }

  methods->Proceed();
}

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

int RunClient() {
  int ret = 0;

  grpc::testing::ChannelCreationFunc channel_creation_func;
  std::string test_case = absl::GetFlag(FLAGS_test_case);
  if (absl::GetFlag(FLAGS_additional_metadata).empty()) {
    channel_creation_func = [test_case]() {
      std::vector<std::unique_ptr<
          grpc::experimental::ClientInterceptorFactoryInterface>>
          factories;
      if (absl::GetFlag(FLAGS_log_metadata_and_status)) {
        factories.emplace_back(
            new grpc::testing::MetadataAndStatusLoggerInterceptorFactory());
      }
      return CreateChannelForTestCase(test_case, std::move(factories));
    };
  } else {
    std::multimap<std::string, std::string> additional_metadata;
    if (!ParseAdditionalMetadataFlag(absl::GetFlag(FLAGS_additional_metadata),
                                     &additional_metadata)) {
      return 1;
    }

    channel_creation_func = [test_case, additional_metadata]() {
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
      return CreateChannelForTestCase(test_case, std::move(factories));
    };
  }

  InteropClient client(channel_creation_func, true,
                       absl::GetFlag(FLAGS_do_not_abort_on_transient_failures));

  std::unordered_map<std::string, std::function<bool()>> actions;
  actions["empty_unary"] = std::bind(&InteropClient::DoEmpty, &client);
  actions["large_unary"] = std::bind(&InteropClient::DoLargeUnary, &client);
  actions["server_compressed_unary"] =
      std::bind(&InteropClient::DoServerCompressedUnary, &client);
  actions["client_compressed_unary"] =
      std::bind(&InteropClient::DoClientCompressedUnary, &client);
  actions["client_streaming"] =
      std::bind(&InteropClient::DoRequestStreaming, &client);
  actions["server_streaming"] =
      std::bind(&InteropClient::DoResponseStreaming, &client);
  actions["server_compressed_streaming"] =
      std::bind(&InteropClient::DoServerCompressedStreaming, &client);
  actions["client_compressed_streaming"] =
      std::bind(&InteropClient::DoClientCompressedStreaming, &client);
  actions["slow_consumer"] =
      std::bind(&InteropClient::DoResponseStreamingWithSlowConsumer, &client);
  actions["half_duplex"] = std::bind(&InteropClient::DoHalfDuplex, &client);
  actions["ping_pong"] = std::bind(&InteropClient::DoPingPong, &client);
  actions["cancel_after_begin"] =
      std::bind(&InteropClient::DoCancelAfterBegin, &client);
  actions["cancel_after_first_response"] =
      std::bind(&InteropClient::DoCancelAfterFirstResponse, &client);
  actions["timeout_on_sleeping_server"] =
      std::bind(&InteropClient::DoTimeoutOnSleepingServer, &client);
  actions["empty_stream"] = std::bind(&InteropClient::DoEmptyStream, &client);
  actions["pick_first_unary"] =
      std::bind(&InteropClient::DoPickFirstUnary, &client);
  if (absl::GetFlag(FLAGS_use_tls)) {
    actions["compute_engine_creds"] =
        std::bind(&InteropClient::DoComputeEngineCreds, &client,
                  absl::GetFlag(FLAGS_default_service_account),
                  absl::GetFlag(FLAGS_oauth_scope));
    actions["jwt_token_creds"] = std::bind(&InteropClient::DoJwtTokenCreds,
                                           &client, GetServiceAccountJsonKey());
    actions["oauth2_auth_token"] =
        std::bind(&InteropClient::DoOauth2AuthToken, &client,
                  absl::GetFlag(FLAGS_default_service_account),
                  absl::GetFlag(FLAGS_oauth_scope));
    actions["per_rpc_creds"] = std::bind(&InteropClient::DoPerRpcCreds, &client,
                                         GetServiceAccountJsonKey());
  }
  if (absl::GetFlag(FLAGS_custom_credentials_type) ==
      "google_default_credentials") {
    actions["google_default_credentials"] =
        std::bind(&InteropClient::DoGoogleDefaultCredentials, &client,
                  absl::GetFlag(FLAGS_default_service_account));
  }
  actions["status_code_and_message"] =
      std::bind(&InteropClient::DoStatusWithMessage, &client);
  actions["special_status_message"] =
      std::bind(&InteropClient::DoSpecialStatusMessage, &client);
  actions["custom_metadata"] =
      std::bind(&InteropClient::DoCustomMetadata, &client);
  actions["unimplemented_method"] =
      std::bind(&InteropClient::DoUnimplementedMethod, &client);
  actions["unimplemented_service"] =
      std::bind(&InteropClient::DoUnimplementedService, &client);
  actions["channel_soak"] = std::bind(
      &InteropClient::DoChannelSoakTest, &client,
      absl::GetFlag(FLAGS_server_host), absl::GetFlag(FLAGS_soak_iterations),
      absl::GetFlag(FLAGS_soak_max_failures),
      absl::GetFlag(FLAGS_soak_per_iteration_max_acceptable_latency_ms),
      absl::GetFlag(FLAGS_soak_min_time_ms_between_rpcs),
      absl::GetFlag(FLAGS_soak_overall_timeout_seconds));
  actions["rpc_soak"] = std::bind(
      &InteropClient::DoRpcSoakTest, &client, absl::GetFlag(FLAGS_server_host),
      absl::GetFlag(FLAGS_soak_iterations),
      absl::GetFlag(FLAGS_soak_max_failures),
      absl::GetFlag(FLAGS_soak_per_iteration_max_acceptable_latency_ms),
      absl::GetFlag(FLAGS_soak_min_time_ms_between_rpcs),
      absl::GetFlag(FLAGS_soak_overall_timeout_seconds));
  actions["long_lived_channel"] =
      std::bind(&InteropClient::DoLongLivedChannelTest, &client,
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

  return ret;
}

}  // namespace testing
}  // namespace grpc
