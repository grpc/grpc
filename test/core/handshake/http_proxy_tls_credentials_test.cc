//
//
// Copyright 2024 gRPC authors.
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

#include "src/core/handshaker/http_connect/http_proxy_tls_credentials.h"

#include <grpc/impl/channel_arg_names.h>

#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(HttpProxyTlsCredentialsTest, EnabledWithDefaultConfig) {
  ChannelArgs args;
  auto creds = CreateHttpProxyTlsCredentials(args);
  // Should create credentials with default (system) root certs
  EXPECT_NE(creds, nullptr);
}

TEST(HttpProxyTlsCredentialsTest, EnabledWithCustomRootCerts) {
  // This is a minimal valid PEM certificate for testing structure
  // In real tests you'd use actual test certificates
  const char* test_root_certs =
      "-----BEGIN CERTIFICATE-----\n"
      "MIIBkTCB+wIJAKHBfpegPjMCMA0GCSqGSIb3DQEBCwUAMBExDzANBgNVBAMMBnVu\n"
      "dXNlZDAeFw0yMzAxMDEwMDAwMDBaFw0yNDAxMDEwMDAwMDBaMBExDzANBgNVBAMM\n"
      "BnVudXNlZDBcMA0GCSqGSIb3DQEBAQUAA0sAMEgCQQC9fEbRszP3EBNDJgPpimda\n"
      "htrhhyqDhKdxMKLJiDzMdRRQx7UECmNq3XDSvJmGcBMTmRmPf9hQJfFLJgOWRNpp\n"
      "AgMBAAEwDQYJKoZIhvcNAQELBQADQQBgcGNhe8LhO+xReGrf+gYz+VrsG0hDjPzQ\n"
      "mKDUYLo2mZL0rSqVXD3WMPBpfVBYI+jlfFU0bUkJMWNf2z7aGcHz\n"
      "-----END CERTIFICATE-----\n";
  auto args = ChannelArgs()
                  .Set(GRPC_ARG_HTTP_PROXY_TLS_ENABLED, true)
                  .Set(GRPC_ARG_HTTP_PROXY_TLS_ROOT_CERTS, test_root_certs);
  auto creds = CreateHttpProxyTlsCredentials(args);
  EXPECT_NE(creds, nullptr);
}

TEST(HttpProxyTlsCredentialsTest, VerifyServerCertDisabled) {
  auto args = ChannelArgs()
                  .Set(GRPC_ARG_HTTP_PROXY_TLS_ENABLED, true)
                  .Set(GRPC_ARG_HTTP_PROXY_TLS_VERIFY_SERVER_CERT, false);
  auto creds = CreateHttpProxyTlsCredentials(args);
  EXPECT_NE(creds, nullptr);
}

TEST(HttpProxyTlsCredentialsTest, CustomServerName) {
  auto args =
      ChannelArgs()
          .Set(GRPC_ARG_HTTP_PROXY_TLS_ENABLED, true)
          .Set(GRPC_ARG_HTTP_PROXY_TLS_SERVER_NAME, "custom-proxy.example.com");
  auto creds = CreateHttpProxyTlsCredentials(args);
  EXPECT_NE(creds, nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
