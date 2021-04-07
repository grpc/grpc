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

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_verifier.h"

#include <gmock/gmock.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>

#include <deque>
#include <list>

#include "src/core/lib/gpr/tmpfile.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/security_connector/tls/tls_security_connector.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

namespace grpc_core {

namespace testing {

class GrpcTlsCertificateVerifierTest : public ::testing::Test {
 protected:
  CertificateVerificationRequest internal_request_;
  HostNameCertificateVerifier hostname_certificate_verifier_;
};

TEST_F(GrpcTlsCertificateVerifierTest, SyncExternalVerifierGood) {
  auto* sync_verifier_ = new SyncExternalVerifier(true);
  ExternalCertificateVerifier core_external_verifier(sync_verifier_->base());
  core_external_verifier.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, SyncExternalVerifierBad) {
  auto* sync_verifier_ = new SyncExternalVerifier(false);
  ExternalCertificateVerifier core_external_verifier(sync_verifier_->base());
  core_external_verifier.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(internal_request_.request.error_details,
               "SyncExternalVerifierBadVerify failed");
}

TEST_F(GrpcTlsCertificateVerifierTest, AsyncExternalVerifierGood) {
  auto* async_verifier = new AsyncExternalVerifier(true, nullptr);
  auto* core_external_verifier =
      new ExternalCertificateVerifier(async_verifier->base());
  core_external_verifier->Verify(
      &internal_request_, [] { gpr_log(GPR_INFO, "Callback is invoked."); });
  // Deleting the verifier will wait for the async thread to be completed.
  // We need to make sure it is completed before checking request's information.
  delete core_external_verifier;
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, AsyncExternalVerifierBad) {
  auto* async_verifier = new AsyncExternalVerifier(false, nullptr);
  auto* core_external_verifier =
      new ExternalCertificateVerifier(async_verifier->base());
  core_external_verifier->Verify(
      &internal_request_, [] { gpr_log(GPR_INFO, "Callback is invoked."); });
  // Deleting the verifier will wait for the async thread to be completed.
  // We need to make sure it is completed before checking request's information.
  delete core_external_verifier;
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(internal_request_.request.error_details,
               "AsyncExternalVerifierBadVerify failed");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierNullTargetName) {
  hostname_certificate_verifier_.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(internal_request_.request.error_details,
               "Target name is not specified.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierInvalidTargetName) {
  internal_request_.request.target_name = gpr_strdup("[foo.com@443");
  hostname_certificate_verifier_.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(internal_request_.request.error_details,
               "Failed to split hostname and port.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierDNSExactCheckGood) {
  internal_request_.request.target_name = gpr_strdup("foo.com:443");
  internal_request_.request.peer_info.san_names.dns_names = new char*[1];
  internal_request_.request.peer_info.san_names.dns_names[0] =
      gpr_strdup("foo.com");
  internal_request_.request.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierDNSWildcardCheckGood) {
  internal_request_.request.target_name = gpr_strdup("foo.bar.com:443");
  internal_request_.request.peer_info.san_names.dns_names = new char*[1];
  internal_request_.request.peer_info.san_names.dns_names[0] =
      gpr_strdup("*.bar.com");
  internal_request_.request.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest,
       HostnameVerifierDNSWildcardCaseInsensitiveCheckGood) {
  internal_request_.request.target_name = gpr_strdup("fOo.bar.cOm:443");
  internal_request_.request.peer_info.san_names.dns_names = new char*[1];
  internal_request_.request.peer_info.san_names.dns_names[0] =
      gpr_strdup("*.BaR.Com");
  internal_request_.request.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierDNSTopWildcardCheckBad) {
  internal_request_.request.target_name = gpr_strdup("foo.com:443");
  internal_request_.request.peer_info.san_names.dns_names = new char*[1];
  internal_request_.request.peer_info.san_names.dns_names[0] = gpr_strdup("*.");
  internal_request_.request.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(internal_request_.request.error_details,
               "Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierDNSExactCheckBad) {
  internal_request_.request.target_name = gpr_strdup("foo.com:443");
  internal_request_.request.peer_info.san_names.dns_names = new char*[1];
  internal_request_.request.peer_info.san_names.dns_names[0] =
      gpr_strdup("bar.com");
  internal_request_.request.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(internal_request_.request.error_details,
               "Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierIpCheckGood) {
  internal_request_.request.target_name = gpr_strdup("192.168.0.1:443");
  internal_request_.request.peer_info.san_names.ip_names = new char*[1];
  internal_request_.request.peer_info.san_names.ip_names[0] =
      gpr_strdup("192.168.0.1");
  internal_request_.request.peer_info.san_names.ip_names_size = 1;
  hostname_certificate_verifier_.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierIpCheckBad) {
  internal_request_.request.target_name = gpr_strdup("192.168.0.1:443");
  internal_request_.request.peer_info.san_names.ip_names = new char*[1];
  internal_request_.request.peer_info.san_names.ip_names[0] =
      gpr_strdup("192.168.1.1");
  internal_request_.request.peer_info.san_names.ip_names_size = 1;
  hostname_certificate_verifier_.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(internal_request_.request.error_details,
               "Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierCommonNameCheckGood) {
  internal_request_.request.target_name = gpr_strdup("foo.com:443");
  internal_request_.request.peer_info.common_name = gpr_strdup("foo.com");
  hostname_certificate_verifier_.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_OK);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierCommonNameCheckBad) {
  internal_request_.request.target_name = gpr_strdup("foo.com:443");
  internal_request_.request.peer_info.common_name = gpr_strdup("bar.com");
  hostname_certificate_verifier_.Verify(&internal_request_, [] {});
  EXPECT_EQ(internal_request_.request.status, GRPC_STATUS_UNAUTHENTICATED);
  EXPECT_STREQ(internal_request_.request.error_details,
               "Hostname Verification Check failed.");
}

}  // namespace testing

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
