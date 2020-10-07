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

#include "src/cpp/client/secure_credentials.h"

namespace {

constexpr const char* kRootCertName = "root_cert_name";
constexpr const char* kRootCertContents = "root_cert_contents";
constexpr const char* kIdentityCertName = "identity_cert_name";
constexpr const char* kIdentityCertPrivateKey = "identity_private_key";
constexpr const char* kIdentityCertContents = "identity_cert_contents";

typedef class ::grpc::experimental::CertificateProviderInterface
    CertificateProviderInterface;
typedef class ::grpc::experimental::StaticDataCertificateProvider
    StaticDataCertificateProvider;
typedef class ::grpc::experimental::TlsCredentialsOptions TlsCredentialsOptions;

}  // namespace

namespace grpc {
namespace testing {

TEST(CredentialsTest, TlsServerCredentialsWithStaticDataCertificateProvider) {
  auto certificate_provider = std::make_shared<StaticDataCertificateProvider>(
      kRootCertContents, kIdentityCertPrivateKey, kIdentityCertContents);
  TlsCredentialsOptions options(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY,
      certificate_provider);
  GPR_ASSERT(options.watch_root_certs());
  GPR_ASSERT(options.set_root_cert_name(kRootCertName));
  GPR_ASSERT(options.watch_identity_key_cert_pairs());
  GPR_ASSERT(options.set_identity_cert_name(kIdentityCertName));
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
  GPR_ASSERT(server_credentials.get() != nullptr);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
