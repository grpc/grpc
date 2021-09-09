//
// Copyright 2021 gRPC authors.
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

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_credentials_options.h>

#include "src/cpp/client/secure_credentials.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/tls_test_utils.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

namespace {

constexpr const char* kRootCertName = "root_cert_name";
constexpr const char* kRootCertContents = "root_cert_contents";
constexpr const char* kIdentityCertName = "identity_cert_name";
constexpr const char* kIdentityCertPrivateKey = "identity_private_key";
constexpr const char* kIdentityCertContents = "identity_cert_contents";

using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::FileWatcherCertificateProvider;
using ::grpc::experimental::HostNameCertificateVerifier;
using ::grpc::experimental::StaticDataCertificateProvider;
using ::grpc::experimental::TlsCustomVerificationCheckRequest;

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
  GPR_ASSERT(server_credentials.get() != nullptr);
}

TEST(TlsCertificateVerifierTest, SyncCertificateVerifierSucceeds) {
  gpr_log(GPR_ERROR,
          "TlsCertificateVerifierTest.SyncCertificateVerifierSucceeds starts");
  grpc_tls_custom_verification_check_request request;
  auto verifier =
      ExternalCertificateVerifier::Create<SyncCertificateVerifier>(true);
  gpr_log(GPR_ERROR, "Inside TlsCertificateVerifierTest.: verifier is created");
  TlsCustomVerificationCheckRequest cpp_request(&request);
  grpc::Status sync_status;
  verifier->Verify(&cpp_request, nullptr, &sync_status);
  gpr_log(GPR_ERROR,
          "Inside TlsCertificateVerifierTest.: verifier->Verify is called");
  EXPECT_TRUE(sync_status.ok())
      << sync_status.error_code() << " " << sync_status.error_message();
  gpr_log(GPR_ERROR,
          "TlsCertificateVerifierTest.SyncCertificateVerifierSucceeds ends");
}

TEST(TlsCertificateVerifierTest, SyncCertificateVerifierFails) {
  grpc_tls_custom_verification_check_request request;
  auto verifier =
      ExternalCertificateVerifier::Create<SyncCertificateVerifier>(false);
  TlsCustomVerificationCheckRequest cpp_request(&request);
  grpc::Status sync_status;
  verifier->Verify(&cpp_request, nullptr, &sync_status);
  EXPECT_EQ(sync_status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
  EXPECT_EQ(sync_status.error_message(), "SyncCertificateVerifier failed");
}

TEST(TlsCertificateVerifierTest, AsyncCertificateVerifierSucceeds) {
  grpc_tls_custom_verification_check_request request;
  auto verifier =
      ExternalCertificateVerifier::Create<AsyncCertificateVerifier>(true);
  TlsCustomVerificationCheckRequest cpp_request(&request);
  std::function<void(grpc::Status)> callback = [](grpc::Status async_status) {
    EXPECT_TRUE(async_status.ok())
        << async_status.error_code() << " " << async_status.error_message();
  };
  grpc::Status sync_status;
  EXPECT_FALSE(verifier->Verify(&cpp_request, callback, &sync_status));
}

TEST(TlsCertificateVerifierTest, AsyncCertificateVerifierFails) {
  grpc_tls_custom_verification_check_request request;
  auto verifier =
      ExternalCertificateVerifier::Create<AsyncCertificateVerifier>(false);
  TlsCustomVerificationCheckRequest cpp_request(&request);
  std::function<void(grpc::Status)> callback = [](grpc::Status async_status) {
    EXPECT_EQ(async_status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
    EXPECT_EQ(async_status.error_message(), "AsyncCertificateVerifier failed");
  };
  grpc::Status sync_status;
  EXPECT_FALSE(verifier->Verify(&cpp_request, callback, &sync_status));
}

TEST(TlsCertificateVerifierTest, HostNameCertificateVerifierSucceeds) {
  grpc_tls_custom_verification_check_request request;
  memset(&request, 0, sizeof(request));
  request.target_name = "foo.bar.com";
  request.peer_info.common_name = "foo.bar.com";
  auto verifier = std::make_shared<HostNameCertificateVerifier>();
  TlsCustomVerificationCheckRequest cpp_request(&request);
  grpc::Status sync_status;
  verifier->Verify(&cpp_request, nullptr, &sync_status);
  EXPECT_TRUE(sync_status.ok())
      << sync_status.error_code() << " " << sync_status.error_message();
}

TEST(TlsCertificateVerifierTest, HostNameCertificateVerifierFails) {
  grpc_tls_custom_verification_check_request request;
  memset(&request, 0, sizeof(request));
  request.target_name = "foo.bar.com";
  request.peer_info.common_name = "foo.baz.com";
  auto verifier = std::make_shared<HostNameCertificateVerifier>();
  TlsCustomVerificationCheckRequest cpp_request(&request);
  grpc::Status sync_status;
  verifier->Verify(&cpp_request, nullptr, &sync_status);
  EXPECT_EQ(sync_status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
  EXPECT_EQ(sync_status.error_message(), "Hostname Verification Check failed.");
}

TEST(TlsCertificateVerifierTest,
     HostNameCertificateVerifierSucceedsMultipleFields) {
  grpc_tls_custom_verification_check_request request;
  memset(&request, 0, sizeof(request));
  request.target_name = "foo.bar.com";
  request.peer_info.common_name = "foo.baz.com";
  char* dns_names[] = {const_cast<char*>("*.bar.com")};
  request.peer_info.san_names.dns_names = dns_names;
  request.peer_info.san_names.dns_names_size = 1;
  auto verifier = std::make_shared<HostNameCertificateVerifier>();
  TlsCustomVerificationCheckRequest cpp_request(&request);
  grpc::Status sync_status;
  verifier->Verify(&cpp_request, nullptr, &sync_status);
  EXPECT_TRUE(sync_status.ok())
      << sync_status.error_code() << " " << sync_status.error_message();
}

TEST(TlsCertificateVerifierTest,
     HostNameCertificateVerifierFailsMultipleFields) {
  grpc_tls_custom_verification_check_request request;
  memset(&request, 0, sizeof(request));
  request.target_name = "foo.bar.com";
  request.peer_info.common_name = "foo.baz.com";
  char* dns_names[] = {const_cast<char*>("*.")};
  request.peer_info.san_names.dns_names = dns_names;
  request.peer_info.san_names.dns_names_size = 1;
  auto verifier = std::make_shared<HostNameCertificateVerifier>();
  TlsCustomVerificationCheckRequest cpp_request(&request);
  grpc::Status sync_status;
  verifier->Verify(&cpp_request, nullptr, &sync_status);
  EXPECT_EQ(sync_status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
  EXPECT_EQ(sync_status.error_message(), "Hostname Verification Check failed.");
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
