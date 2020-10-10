//
// Copyright 2020 gRPC authors.
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

#include <gmock/gmock.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <gtest/gtest.h>

#include <memory>

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include "src/cpp/client/secure_credentials.h"

namespace {

constexpr const char* kRootCertName = "root_cert_name";
constexpr const char* kRootCertContents = "root_cert_contents";
constexpr const char* kIdentityCertName = "identity_cert_name";
constexpr const char* kIdentityCertPrivateKey = "identity_private_key";
constexpr const char* kIdentityCertContents = "identity_cert_contents";

using ::grpc::experimental::CertificateProviderInterface;
using ::grpc::experimental::StaticDataCertificateProvider;
using ::grpc::experimental::TlsCredentialsOptions;

}  // namespace

namespace grpc {
namespace testing {
namespace {

TEST(CredentialsTest, TlsServerCredentialsWithStaticDataCertificateProvider) {
  auto certificate_provider = std::make_shared<StaticDataCertificateProvider>(
      kRootCertContents, kIdentityCertPrivateKey, kIdentityCertContents);
  TlsCredentialsOptions options(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY,
      certificate_provider);
  options.WatchRootCerts();
  options.SetRootCertName(kRootCertName);
  options.WatchIdentityKeyCertPairs();
  options.SetIdentityCertName(kIdentityCertName);
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
  GPR_ASSERT(server_credentials.get() != nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
