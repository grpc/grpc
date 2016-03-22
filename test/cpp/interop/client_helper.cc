/*
 *
 * Copyright 2015, Google Inc.
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

#include "test/cpp/interop/client_helper.h"

#include <unistd.h>

#include <fstream>
#include <memory>
#include <sstream>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <gflags/gflags.h>
#include <grpc++/channel.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>

#include "src/cpp/client/secure_credentials.h"
#include "test/core/security/oauth2_utils.h"
#include "test/cpp/util/create_test_channel.h"

DECLARE_bool(use_tls);
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

std::shared_ptr<Channel> CreateChannelForTestCase(
    const grpc::string& test_case) {
  GPR_ASSERT(FLAGS_server_port);
  const int host_port_buf_size = 1024;
  char host_port[host_port_buf_size];
  snprintf(host_port, host_port_buf_size, "%s:%d", FLAGS_server_host.c_str(),
           FLAGS_server_port);

  if (test_case == "compute_engine_creds") {
    std::shared_ptr<CallCredentials> creds;
    GPR_ASSERT(FLAGS_use_tls);
    creds = GoogleComputeEngineCredentials();
    return CreateTestChannel(host_port, FLAGS_server_host_override,
                             FLAGS_use_tls, !FLAGS_use_test_ca, creds);
  } else if (test_case == "jwt_token_creds") {
    std::shared_ptr<CallCredentials> creds;
    GPR_ASSERT(FLAGS_use_tls);
    grpc::string json_key = GetServiceAccountJsonKey();
    std::chrono::seconds token_lifetime = std::chrono::hours(1);
    creds =
        ServiceAccountJWTAccessCredentials(json_key, token_lifetime.count());
    return CreateTestChannel(host_port, FLAGS_server_host_override,
                             FLAGS_use_tls, !FLAGS_use_test_ca, creds);
  } else if (test_case == "oauth2_auth_token") {
    grpc::string raw_token = GetOauth2AccessToken();
    std::shared_ptr<CallCredentials> creds = AccessTokenCredentials(raw_token);
    return CreateTestChannel(host_port, FLAGS_server_host_override,
                             FLAGS_use_tls, !FLAGS_use_test_ca, creds);
  } else {
    return CreateTestChannel(host_port, FLAGS_server_host_override,
                             FLAGS_use_tls, !FLAGS_use_test_ca);
  }
}

}  // namespace testing
}  // namespace grpc
