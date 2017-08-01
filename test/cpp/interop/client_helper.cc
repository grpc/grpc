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

#include "test/cpp/interop/client_helper.h"

#include <fstream>
#include <memory>
#include <sstream>

#include <gflags/gflags.h>
#include <grpc++/channel.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/cpp/client/secure_credentials.h"
#include "test/core/security/oauth2_utils.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/util/test_credentials_provider.h"

DECLARE_bool(use_tls);
DECLARE_string(custom_credentials_type);
DECLARE_bool(use_test_ca);
DECLARE_int32(server_port);
DECLARE_string(server_host);
DECLARE_string(server_host_override);
DECLARE_string(test_case);
DECLARE_string(default_service_account);
DECLARE_string(service_account_key_file);
DECLARE_string(oauth_scope);

namespace grpc {
namespace testing {

grpc::string GetServiceAccountJsonKey() {
  static grpc::string json_key;
  if (json_key.empty()) {
    std::ifstream json_key_file(FLAGS_service_account_key_file);
    std::stringstream key_stream;
    key_stream << json_key_file.rdbuf();
    json_key = key_stream.str();
  }
  return json_key;
}

grpc::string GetOauth2AccessToken() {
  std::shared_ptr<CallCredentials> creds = GoogleComputeEngineCredentials();
  SecureCallCredentials* secure_creds =
      dynamic_cast<SecureCallCredentials*>(creds.get());
  GPR_ASSERT(secure_creds != nullptr);
  grpc_call_credentials* c_creds = secure_creds->GetRawCreds();
  char* token = grpc_test_fetch_oauth2_token_with_credentials(c_creds);
  GPR_ASSERT(token != nullptr);
  gpr_log(GPR_INFO, "Get raw oauth2 access token: %s", token);
  grpc::string access_token(token + sizeof("Bearer ") - 1);
  gpr_free(token);
  return access_token;
}

void UpdateActions(
    std::unordered_map<grpc::string, std::function<bool()>>* actions) {}

std::shared_ptr<Channel> CreateChannelForTestCase(
    const grpc::string& test_case) {
  GPR_ASSERT(FLAGS_server_port);
  const int host_port_buf_size = 1024;
  char host_port[host_port_buf_size];
  snprintf(host_port, host_port_buf_size, "%s:%d", FLAGS_server_host.c_str(),
           FLAGS_server_port);

  std::shared_ptr<CallCredentials> creds;
  if (test_case == "compute_engine_creds") {
    GPR_ASSERT(FLAGS_use_tls);
    creds = GoogleComputeEngineCredentials();
    GPR_ASSERT(creds);
  } else if (test_case == "jwt_token_creds") {
    GPR_ASSERT(FLAGS_use_tls);
    grpc::string json_key = GetServiceAccountJsonKey();
    std::chrono::seconds token_lifetime = std::chrono::hours(1);
    creds =
        ServiceAccountJWTAccessCredentials(json_key, token_lifetime.count());
    GPR_ASSERT(creds);
  } else if (test_case == "oauth2_auth_token") {
    grpc::string raw_token = GetOauth2AccessToken();
    creds = AccessTokenCredentials(raw_token);
    GPR_ASSERT(creds);
  }
  if (FLAGS_custom_credentials_type.empty()) {
    return CreateTestChannel(host_port, FLAGS_server_host_override,
                             FLAGS_use_tls, !FLAGS_use_test_ca, creds);
  } else {
    return CreateTestChannel(host_port, FLAGS_custom_credentials_type, creds);
  }
}

}  // namespace testing
}  // namespace grpc
