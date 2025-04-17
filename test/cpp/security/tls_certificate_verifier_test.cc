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

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/security/tls_credentials_options.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/cpp/client/secure_credentials.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/util/tls_test_utils.h"

namespace {

using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::HostNameCertificateVerifier;
using ::grpc::experimental::NoOpCertificateVerifier;
using ::grpc::experimental::TlsCustomVerificationCheckRequest;

}  // namespace

namespace grpc {
namespace testing {
namespace {

TEST(TlsCertificateVerifierTest, SyncCertificateVerifierSucceeds) {
  grpc_tls_custom_verification_check_request request;
  auto verifier =
      ExternalCertificateVerifier::Create<SyncCertificateVerifier>(true);
  TlsCustomVerificationCheckRequest cpp_request(&request);
  grpc::Status sync_status;
  verifier->Verify(&cpp_request, nullptr, &sync_status);
  EXPECT_TRUE(sync_status.ok())
      << sync_status.error_code() << " " << sync_status.error_message();
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

TEST(TlsCertificateVerifierTest, NoOpCertificateVerifierSucceeds) {
  grpc_tls_custom_verification_check_request request;
  memset(&request, 0, sizeof(request));
  auto verifier = std::make_shared<NoOpCertificateVerifier>();
  TlsCustomVerificationCheckRequest cpp_request(&request);
  grpc::Status sync_status;
  verifier->Verify(&cpp_request, nullptr, &sync_status);
  EXPECT_TRUE(sync_status.ok())
      << sync_status.error_code() << " " << sync_status.error_message();
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

TEST(TlsCertificateVerifierTest, VerifiedRootCertSubjectVerifierSucceeds) {
  grpc_tls_custom_verification_check_request request;
  constexpr char kExpectedSubject[] =
      "CN=testca,O=Internet Widgits Pty Ltd,ST=Some-State,C=AU";
  request.peer_info.verified_root_cert_subject = kExpectedSubject;
  auto verifier =
      ExternalCertificateVerifier::Create<VerifiedRootCertSubjectVerifier>(
          kExpectedSubject);
  TlsCustomVerificationCheckRequest cpp_request(&request);
  grpc::Status sync_status;
  bool is_sync = verifier->Verify(&cpp_request, nullptr, &sync_status);
  EXPECT_TRUE(is_sync);
  EXPECT_TRUE(sync_status.ok())
      << sync_status.error_code() << " " << sync_status.error_message();
}

TEST(TlsCertificateVerifierTest, VerifiedRootCertSubjectVerifierFailsNull) {
  grpc_tls_custom_verification_check_request request;
  constexpr char kExpectedSubject[] =
      "CN=testca,O=Internet Widgits Pty Ltd,ST=Some-State,C=AU";
  request.peer_info.verified_root_cert_subject = nullptr;
  auto verifier =
      ExternalCertificateVerifier::Create<VerifiedRootCertSubjectVerifier>(
          kExpectedSubject);
  TlsCustomVerificationCheckRequest cpp_request(&request);
  EXPECT_EQ(cpp_request.verified_root_cert_subject(), "");
  grpc::Status sync_status;
  verifier->Verify(&cpp_request, nullptr, &sync_status);
  EXPECT_EQ(sync_status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
  EXPECT_EQ(sync_status.error_message(),
            "VerifiedRootCertSubjectVerifier failed");
}

TEST(TlsCertificateVerifierTest, VerifiedRootCertSubjectVerifierFailsMismatch) {
  grpc_tls_custom_verification_check_request request;
  constexpr char kExpectedSubject[] =
      "CN=testca,O=Internet Widgits Pty Ltd,ST=Some-State,C=AU";
  request.peer_info.verified_root_cert_subject = "BAD_SUBJECT";
  auto verifier =
      ExternalCertificateVerifier::Create<VerifiedRootCertSubjectVerifier>(
          kExpectedSubject);
  TlsCustomVerificationCheckRequest cpp_request(&request);
  grpc::Status sync_status;
  verifier->Verify(&cpp_request, nullptr, &sync_status);
  EXPECT_EQ(sync_status.error_code(), grpc::StatusCode::UNAUTHENTICATED);
  EXPECT_EQ(sync_status.error_message(),
            "VerifiedRootCertSubjectVerifier failed");
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
