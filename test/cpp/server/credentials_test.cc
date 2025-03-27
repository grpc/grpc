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

#include <grpc/grpc.h>
#include <grpc/grpc_crl_provider.h>
#include <grpc/grpc_security.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <grpcpp/security/tls_crl_provider.h>

#include <memory>

#include "absl/log/check.h"
#include "gtest/gtest.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "test/cpp/util/tls_test_utils.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"
#define CRL_DIR_PATH "test/core/tsi/test_creds/crl_data/crls"
#define MALFORMED_CERT_PATH "src/core/tsi/test_creds/malformed-cert.pem"

namespace {

constexpr const char* kRootCertName = "root_cert_name";
constexpr const char* kRootCertContents = "root_cert_contents";
constexpr const char* kIdentityCertName = "identity_cert_name";
constexpr const char* kIdentityCertPrivateKey = "identity_private_key";
constexpr const char* kIdentityCertContents = "identity_cert_contents";

using ::grpc::experimental::CreateStaticCrlProvider;
using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::FileWatcherCertificateProvider;
using ::grpc::experimental::NoOpCertificateVerifier;
using ::grpc::experimental::StaticDataCertificateProvider;
using ::grpc::experimental::TlsServerCredentials;
using ::grpc::experimental::TlsServerCredentialsOptions;
using ::grpc_core::testing::GetFileContents;

}  // namespace

namespace grpc {
namespace testing {
namespace {

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
  CHECK_NE(server_credentials.get(), nullptr);
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
  CHECK_NE(server_credentials.get(), nullptr);
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
  CHECK_NE(server_credentials.get(), nullptr);
}

TEST(CredentialsTest,
     StaticDataCertificateProviderValidationSuccessWithAllCredentials) {
  std::string root_certificates = GetFileContents(CA_CERT_PATH);
  experimental::IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key = GetFileContents(SERVER_KEY_PATH);
  key_cert_pair.certificate_chain = GetFileContents(SERVER_CERT_PATH);
  StaticDataCertificateProvider provider(root_certificates, {key_cert_pair});
  EXPECT_EQ(provider.ValidateCredentials(), absl::OkStatus());
}

TEST(CredentialsTest, StaticDataCertificateProviderWithMalformedRoot) {
  std::string root_certificates = GetFileContents(MALFORMED_CERT_PATH);
  experimental::IdentityKeyCertPair key_cert_pair;
  key_cert_pair.private_key = GetFileContents(SERVER_KEY_PATH);
  key_cert_pair.certificate_chain = GetFileContents(SERVER_CERT_PATH);
  StaticDataCertificateProvider provider(root_certificates, {key_cert_pair});
  EXPECT_EQ(provider.ValidateCredentials(),
            absl::FailedPreconditionError(
                "Failed to parse root certificates as PEM: Invalid PEM."));
}

TEST(CredentialsTest,
     FileWatcherCertificateProviderValidationSuccessWithAllCredentials) {
  FileWatcherCertificateProvider provider(SERVER_KEY_PATH, SERVER_CERT_PATH,
                                          CA_CERT_PATH, 1);
  EXPECT_EQ(provider.ValidateCredentials(), absl::OkStatus());
}

TEST(CredentialsTest, FileWatcherCertificateProviderWithMalformedRoot) {
  FileWatcherCertificateProvider provider(SERVER_KEY_PATH, SERVER_CERT_PATH,
                                          MALFORMED_CERT_PATH, 1);
  EXPECT_EQ(provider.ValidateCredentials(),
            absl::FailedPreconditionError(
                "Failed to parse root certificates as PEM: Invalid PEM."));
}

TEST(CredentialsTest, TlsServerCredentialsWithCrlChecking) {
  auto certificate_provider = std::make_shared<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  grpc::experimental::TlsServerCredentialsOptions options(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name(kRootCertName);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  options.set_cert_request_type(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  options.set_crl_directory(CRL_DIR_PATH);
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
  CHECK_NE(server_credentials.get(), nullptr);
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
  CHECK_NE(server_credentials.get(), nullptr);
}

TEST(CredentialsTest, TlsServerCredentialsWithSyncExternalVerifier) {
  auto verifier =
      ExternalCertificateVerifier::Create<SyncCertificateVerifier>(true);
  auto certificate_provider = std::make_shared<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  grpc::experimental::TlsServerCredentialsOptions options(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name(kRootCertName);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  options.set_cert_request_type(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  options.set_certificate_verifier(verifier);
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
  CHECK_NE(server_credentials.get(), nullptr);
}

TEST(CredentialsTest, TlsServerCredentialsWithAsyncExternalVerifier) {
  auto verifier =
      ExternalCertificateVerifier::Create<AsyncCertificateVerifier>(true);
  auto certificate_provider = std::make_shared<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  grpc::experimental::TlsServerCredentialsOptions options(certificate_provider);
  options.watch_root_certs();
  options.set_root_cert_name(kRootCertName);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  options.set_cert_request_type(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  options.set_certificate_verifier(verifier);
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
  CHECK_NE(server_credentials.get(), nullptr);
}

TEST(CredentialsTest, TlsServerCredentialsWithCrlProvider) {
  auto provider = experimental::CreateStaticCrlProvider({});
  ASSERT_TRUE(provider.ok());
  auto certificate_provider = std::make_shared<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  grpc::experimental::TlsServerCredentialsOptions options(certificate_provider);
  options.set_crl_provider(*provider);
  auto channel_credentials = grpc::experimental::TlsServerCredentials(options);
  CHECK_NE(channel_credentials.get(), nullptr);
}

TEST(CredentialsTest, TlsServerCredentialsWithCrlProviderAndDirectory) {
  auto provider = experimental::CreateStaticCrlProvider({});
  ASSERT_TRUE(provider.ok());
  auto certificate_provider = std::make_shared<FileWatcherCertificateProvider>(
      SERVER_KEY_PATH, SERVER_CERT_PATH, CA_CERT_PATH, 1);
  grpc::experimental::TlsServerCredentialsOptions options(certificate_provider);
  options.set_crl_directory(CRL_DIR_PATH);
  options.set_crl_provider(*provider);
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
  //   TODO(gtcooke94) - behavior might change to make this return nullptr in
  //   the future
  CHECK_NE(server_credentials, nullptr);
}

TEST(CredentialsTest, TlsCredentialsOptionsDoesNotLeak) {
  auto provider = std::make_shared<StaticDataCertificateProvider>("root-pem");
  TlsServerCredentialsOptions options(provider);
  (void)options;
}

TEST(CredentialsTest, MultipleOptionsOneCertificateProviderDoesNotLeak) {
  auto provider = std::make_shared<StaticDataCertificateProvider>("root-pem");
  TlsServerCredentialsOptions options_1(provider);
  (void)options_1;
  TlsServerCredentialsOptions options_2(provider);
  (void)options_2;
}

TEST(CredentialsTest, MultipleOptionsOneCertificateVerifierDoesNotLeak) {
  auto provider = std::make_shared<StaticDataCertificateProvider>("root-pem");
  auto verifier = std::make_shared<NoOpCertificateVerifier>();
  TlsServerCredentialsOptions options_1(provider);
  options_1.set_certificate_verifier(verifier);
  TlsServerCredentialsOptions options_2(provider);
  options_2.set_certificate_verifier(verifier);
}

TEST(CredentialsTest, MultipleOptionsOneCrlProviderDoesNotLeak) {
  auto provider = std::make_shared<StaticDataCertificateProvider>("root-pem");
  auto crl_provider = CreateStaticCrlProvider(/*crls=*/{});
  EXPECT_TRUE(crl_provider.ok());
  TlsServerCredentialsOptions options_1(provider);
  options_1.set_crl_provider(*crl_provider);
  TlsServerCredentialsOptions options_2(provider);
  options_2.set_crl_provider(*crl_provider);
}

TEST(CredentialsTest, TlsServerCredentialsDoesNotLeak) {
  auto provider = std::make_shared<StaticDataCertificateProvider>("root-pem");
  TlsServerCredentialsOptions options(provider);
  auto server_creds = TlsServerCredentials(options);
  EXPECT_NE(server_creds, nullptr);
}

TEST(CredentialsTest, MultipleServerCredentialsOneOptionsDoesNotLeak) {
  auto provider = std::make_shared<StaticDataCertificateProvider>("root-pem");
  TlsServerCredentialsOptions options(provider);
  auto server_creds_1 = TlsServerCredentials(options);
  EXPECT_NE(server_creds_1, nullptr);
  auto server_creds_2 = TlsServerCredentials(options);
  EXPECT_NE(server_creds_2, nullptr);
}

TEST(CredentialsTest,
     MultipleServerCredentialsOneCertificateVerifierDoesNotLeak) {
  auto provider = std::make_shared<StaticDataCertificateProvider>("root-pem");
  TlsServerCredentialsOptions options(provider);
  auto verifier = std::make_shared<NoOpCertificateVerifier>();
  options.set_certificate_verifier(verifier);
  auto server_creds_1 = TlsServerCredentials(options);
  EXPECT_NE(server_creds_1, nullptr);
  auto server_creds_2 = TlsServerCredentials(options);
  EXPECT_NE(server_creds_2, nullptr);
}

TEST(CredentialsTest, MultipleServerCredentialsOneCrlProviderDoesNotLeak) {
  auto provider = std::make_shared<StaticDataCertificateProvider>("root-pem");
  TlsServerCredentialsOptions options(provider);
  auto crl_provider = CreateStaticCrlProvider(/*crls=*/{});
  EXPECT_TRUE(crl_provider.ok());
  options.set_crl_provider(*crl_provider);
  auto server_creds_1 = TlsServerCredentials(options);
  EXPECT_NE(server_creds_1, nullptr);
  auto server_creds_2 = TlsServerCredentials(options);
  EXPECT_NE(server_creds_2, nullptr);
}

TEST(CredentialsTest, TlsServerCredentialsWithGoodMinMaxTlsVersions) {
  grpc::experimental::TlsServerCredentialsOptions options(
      /*certificate_provider=*/nullptr);
  options.set_min_tls_version(grpc_tls_version::TLS1_2);
  options.set_max_tls_version(grpc_tls_version::TLS1_3);
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
  EXPECT_NE(server_credentials, nullptr);
}

TEST(CredentialsTest, TlsServerCredentialsWithBadMinMaxTlsVersions) {
  grpc::experimental::TlsServerCredentialsOptions options(
      /*certificate_provider=*/nullptr);
  options.set_min_tls_version(grpc_tls_version::TLS1_3);
  options.set_max_tls_version(grpc_tls_version::TLS1_2);
  auto server_credentials = grpc::experimental::TlsServerCredentials(options);
  EXPECT_EQ(server_credentials, nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
