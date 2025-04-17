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

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <fstream>
#include <memory>
#include <sstream>

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "test/core/credentials/call/oauth2/oauth2_utils.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/util/test_credentials_provider.h"

ABSL_DECLARE_FLAG(std::string, custom_credentials_type);
ABSL_DECLARE_FLAG(std::string, default_service_account);
ABSL_DECLARE_FLAG(std::string, oauth_scope);
ABSL_DECLARE_FLAG(std::string, service_account_key_file);
ABSL_DECLARE_FLAG(std::string, server_host);
ABSL_DECLARE_FLAG(std::string, server_host_override);
ABSL_DECLARE_FLAG(int32_t, server_port);
ABSL_DECLARE_FLAG(std::string, test_case);
ABSL_DECLARE_FLAG(bool, use_alts);
ABSL_DECLARE_FLAG(bool, use_test_ca);
ABSL_DECLARE_FLAG(bool, use_tls);

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
  char* token = grpc_test_fetch_oauth2_token_with_credentials(creds->c_creds_);
  CHECK_NE(token, nullptr);
  LOG(INFO) << "Get raw oauth2 access token: " << token;
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
        interceptor_creators,
    ChannelArguments channel_args) {
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
    return CreateTestChannel(
        server_uri, absl::GetFlag(FLAGS_server_host_override), security_type,
        !absl::GetFlag(FLAGS_use_test_ca), creds, channel_args,
        std::move(interceptor_creators));
  } else {
    if (interceptor_creators.empty()) {
      return CreateTestChannel(server_uri,
                               absl::GetFlag(FLAGS_custom_credentials_type), "",
                               false, creds, channel_args);
    } else {
      return CreateTestChannel(
          server_uri, absl::GetFlag(FLAGS_custom_credentials_type), creds,
          std::move(interceptor_creators), channel_args);
    }
  }
}

static void log_metadata_entry(const std::string& prefix,
                               const grpc::string_ref& key,
                               const grpc::string_ref& value) {
  std::string key_str(key.begin(), key.end());
  std::string value_str(value.begin(), value.end());
  if (absl::EndsWith(key_str, "-bin")) {
    value_str = absl::Base64Escape(value_str);
  }
  LOG(ERROR) << prefix << " " << key_str << ": " << value_str;
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
    LOG(ERROR) << "GRPC_STATUS " << status->error_code();
    LOG(ERROR) << "GRPC_ERROR_MESSAGE " << status->error_message();
  }

  methods->Proceed();
}

}  // namespace testing
}  // namespace grpc
