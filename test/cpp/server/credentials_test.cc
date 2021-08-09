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
#include <stdio.h>
#include <string.h>

#include <memory>

#include "src/cpp/client/secure_credentials.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "include/grpc/grpc_security.h"
#include "test/core/util/tls_utils.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"
#define CA_CERT_PATH_2 "src/core/tsi/test_creds/multi-domain.pem"
#define SERVER_CERT_PATH_2 "src/core/tsi/test_creds/server0.pem"
#define SERVER_KEY_PATH_2 "src/core/tsi/test_creds/server0.key"

namespace {

constexpr const char* kRootCertName = "root_cert_name";
constexpr const char* kRootCertContents = "root_cert_contents";
constexpr const char* kIdentityCertName = "identity_cert_name";
constexpr const char* kIdentityCertPrivateKey = "identity_private_key";
constexpr const char* kIdentityCertName2 = "identity_cert_name_2";
constexpr const char* kIdentityCertPrivateKey2 = "identity_private_key_2";
constexpr const char* kIdentityCertContents = "identity_cert_contents";

using ::grpc::experimental::FileWatcherCertificateProvider;
using ::grpc::experimental::StaticDataCertificateProvider;
//using ::testing::Test::GrpcTlsCertificateProviderTest;

}  // namespace

namespace grpc {
namespace testing {
namespace {

class GrpcTlsCertificateKeyMatchTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_cert_ = grpc_core::testing::GetFileContents(CA_CERT_PATH);
    cert_chain_ = grpc_core::testing::GetFileContents(SERVER_CERT_PATH);
    private_key_ = grpc_core::testing::GetFileContents(SERVER_KEY_PATH);
    root_cert_2_ = grpc_core::testing::GetFileContents(CA_CERT_PATH_2);
    cert_chain_2_ = grpc_core::testing::GetFileContents(SERVER_CERT_PATH_2);
    private_key_2_ = grpc_core::testing::GetFileContents(SERVER_KEY_PATH_2);
  }

  std::string root_cert_;
  std::string private_key_;
  std::string cert_chain_;
  std::string root_cert_2_;
  std::string private_key_2_;
  std::string cert_chain_2_;
};

TEST(
    CredentialsTest,
    TlsServerCredentialsWithStaticDataCertificateProviderLoadingRootAndIdentity) {
  experimental::IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key = kIdentityCertPrivateKey;
  key_cert_pair.certificate_chain = kIdentityCertContents;
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  identity_key_cert_pairs.emplace_back(key_cert_pair);
  auto certificate_provider = std::make_shared<StaticDataCertificateProvider>(
      kRootCertContents, identity_key_cert_pairs);
  grpc::experimental::TlsServerCredentialsOptions options(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name(kRootCertName);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  options.set_cert_request_type(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
  GPR_ASSERT(server_credentials.get() != nullptr);
}

// ServerCredentials should always have identity credential presented.
// Otherwise gRPC stack will fail.
TEST(CredentialsTest,
     TlsServerCredentialsWithStaticDataCertificateProviderLoadingIdentityOnly) {
  experimental::IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key = kIdentityCertPrivateKey;
  key_cert_pair.certificate_chain = kIdentityCertContents;
  std::vector<experimental::IdentityKeyCertPair> identity_key_cert_pairs;
  // Adding two key_cert_pair(s) should still work.
  identity_key_cert_pairs.emplace_back(key_cert_pair);
  identity_key_cert_pairs.emplace_back(key_cert_pair);
  auto certificate_provider =
      std::make_shared<StaticDataCertificateProvider>(identity_key_cert_pairs);
  grpc::experimental::TlsServerCredentialsOptions options(certificate_provider);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  options.set_cert_request_type(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
  GPR_ASSERT(server_credentials.get() != nullptr);
}

TEST(
    CredentialsTest,
    TlsServerCredentialsWithFileWatcherCertificateProviderLoadingRootAndIdentity) {
  auto certificate_provider = std::make_shared<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  grpc::experimental::TlsServerCredentialsOptions options(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name(kRootCertName);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  options.set_cert_request_type(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
  GPR_ASSERT(server_credentials.get() != nullptr);
}

// ServerCredentials should always have identity credential presented.
// Otherwise gRPC stack will fail.
TEST(
    CredentialsTest,
    TlsServerCredentialsWithFileWatcherCertificateProviderLoadingIdentityOnly) {
  auto certificate_provider = std::make_shared<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, 1);
  grpc::experimental::TlsServerCredentialsOptions options(certificate_provider);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  options.set_cert_request_type(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
  GPR_ASSERT(server_credentials.get() != nullptr);
}

TEST_F(
    GrpcTlsCertificateKeyMatchTest,
    APIWrapperForPrivateKeyAndCertificateMatchForFailedKeyCertMatchOnEmptyPrivateKey){
  const char* error_details;
  grpc_status_code matched = grpc_tls_certificate_key_match(
      /*private_key=*/"", cert_chain_.c_str(), &error_details);
  EXPECT_EQ(matched, static_cast<grpc_status_code>(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ("Private key string is empty.", std::string(error_details));
  gpr_free(const_cast<char*>(error_details));
}

TEST_F(
    GrpcTlsCertificateKeyMatchTest,
    APIWrapperForPrivateKeyAndCertificateMatchForFailedKeyCertMatchOnEmptyCertificate){
  const char* error_details;
  grpc_status_code matched = grpc_tls_certificate_key_match(
      private_key_.c_str(), /*cert_chain=*/"", &error_details);
  EXPECT_EQ(matched, static_cast<grpc_status_code>(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ("Certificate string is empty.", std::string(error_details));
  gpr_free(const_cast<char*>(error_details));
}

TEST_F(
    GrpcTlsCertificateKeyMatchTest,
    APIWrapperForPrivateKeyAndCertificateMatchForFailedKeyCertMatchOnInvalidCertFormat){
  const char* error_details;
  grpc_status_code matched = grpc_tls_certificate_key_match(
      private_key_.c_str(), "invalid_certificate", &error_details);
  EXPECT_EQ(matched, static_cast<grpc_status_code>(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ("Conversion from PEM string to X509 failed.", std::string(error_details));
  gpr_free(const_cast<char*>(error_details));
}

TEST_F(
    GrpcTlsCertificateKeyMatchTest,
    APIWrapperForPrivateKeyAndCertificateMatchForFailedKeyCertMatchOnInvalidKeyFormat){
  const char* error_details;
  grpc_status_code matched = grpc_tls_certificate_key_match(
      "invalid_private_key", cert_chain_2_.c_str(), &error_details);
  EXPECT_EQ(matched, static_cast<grpc_status_code>(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ("Conversion from PEM string to EVP_PKEY failed.", std::string(error_details));
  gpr_free(const_cast<char*>(error_details));
}

TEST_F(
    GrpcTlsCertificateKeyMatchTest,
    APIWrapperForPrivateKeyAndCertificateMatchForSuccessfulKeyCertMatch){
  const char* error_details;
  grpc_status_code matched = grpc_tls_certificate_key_match(
      private_key_2_.c_str(), cert_chain_2_.c_str(), &error_details);
  EXPECT_EQ("", std::string(error_details));
  EXPECT_EQ(matched, static_cast<grpc_status_code>(absl::StatusCode::kOk));
}

TEST_F(
    GrpcTlsCertificateKeyMatchTest,
    APIWrapperForPrivateKeyAndCertificateMatchForFailedKeyCertMatchOnInvalidPair){
  const char* error_details;
  grpc_status_code matched = grpc_tls_certificate_key_match(
      private_key_2_.c_str(), cert_chain_.c_str(), &error_details);
  gpr_log(GPR_ERROR,
          "CODE REACHED: %s",
          error_details);
  EXPECT_EQ("Certificate-key mismatch", std::string(error_details));
  EXPECT_EQ(matched, static_cast<grpc_status_code>(absl::StatusCode::kInvalidArgument));
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
