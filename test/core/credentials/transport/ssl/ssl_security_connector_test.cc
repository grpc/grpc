//
//
// Copyright 2026 gRPC authors.
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

#include "src/core/credentials/transport/ssl/ssl_security_connector.h"

#include <grpc/grpc_security.h>

#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

constexpr absl::string_view kServerCertPath =
    "src/core/tsi/test_creds/server1.pem";
constexpr absl::string_view kServerKeyPath =
    "src/core/tsi/test_creds/server1.key";

TEST(SslSecurityConnectorTest, ServerSecurityConnectorCreationWithAlpnString) {
  std::string server_cert =
      grpc_core::testing::GetFileContents(std::string(kServerCertPath));
  std::string server_key =
      grpc_core::testing::GetFileContents(std::string(kServerKeyPath));
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key.c_str(),
                                                  server_cert.c_str()};
  grpc_server_credentials* creds = grpc_ssl_server_credentials_create(
      nullptr, &pem_key_cert_pair, 1, 0, nullptr);
  // Create security connector
  grpc_core::ChannelArgs args;
  grpc_core::RefCountedPtr<grpc_server_security_connector> sc =
      creds->create_security_connector(
          args.Set(GRPC_ARG_TRANSPORT_PROTOCOLS, "h2"));
  ASSERT_NE(sc, nullptr);
  grpc_server_credentials_release(creds);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
