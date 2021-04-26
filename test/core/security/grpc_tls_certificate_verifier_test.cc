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
  void SetUp() override {
    PendingVerifierRequest::PendingVerifierRequestInit(&request_);
  }

  void TearDown() override {
    PendingVerifierRequest::PendingVerifierRequestDestroy(&request_);
  }

  grpc_tls_custom_verification_check_request request_;
  HostNameCertificateVerifier hostname_certificate_verifier_;
};

TEST_F(GrpcTlsCertificateVerifierTest, SyncExternalVerifierSucceeds) {
  auto* sync_verifier = new SyncExternalVerifier(true);
  ExternalCertificateVerifier core_external_verifier(sync_verifier->base());
  absl::Status sync_status;
  core_external_verifier.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_TRUE(sync_status.ok());
}

TEST_F(GrpcTlsCertificateVerifierTest, SyncExternalVerifierFails) {
  auto* sync_verifier = new SyncExternalVerifier(false);
  ExternalCertificateVerifier core_external_verifier(sync_verifier->base());
  absl::Status sync_status;
  core_external_verifier.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: SyncExternalVerifierBadVerify failed");
}

TEST_F(GrpcTlsCertificateVerifierTest, AsyncExternalVerifierSucceeds) {
  absl::Status sync_status;
  gpr_event event;
  gpr_event_init(&event);
  auto* async_verifier = new AsyncExternalVerifier(true, &event);
  auto* core_external_verifier =
      new ExternalCertificateVerifier(async_verifier->base());
  core_external_verifier->Verify(
      &request_,
      [](absl::Status async_status) {
        gpr_log(GPR_INFO, "Callback is invoked.");
        EXPECT_TRUE(async_status.ok());
      },
      &sync_status);
  // Wait for the async callback to be completed.
  gpr_event_wait(&event, gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                      gpr_time_from_seconds(5, GPR_TIMESPAN)));
  delete core_external_verifier;
}

TEST_F(GrpcTlsCertificateVerifierTest, AsyncExternalVerifierFails) {
  absl::Status sync_status;
  gpr_event event;
  gpr_event_init(&event);
  auto* async_verifier = new AsyncExternalVerifier(false, &event);
  auto* core_external_verifier =
      new ExternalCertificateVerifier(async_verifier->base());
  core_external_verifier->Verify(
      &request_,
      [](absl::Status async_status) {
        gpr_log(GPR_INFO, "Callback is invoked.");
        EXPECT_EQ(async_status.code(), absl::StatusCode::kUnauthenticated);
        EXPECT_EQ(async_status.ToString(),
                  "UNAUTHENTICATED: AsyncExternalVerifierBadVerify failed");
      },
      &sync_status);
  // Wait for the async callback to be completed.
  gpr_event_wait(&event, gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                      gpr_time_from_seconds(5, GPR_TIMESPAN)));
  delete core_external_verifier;
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierNullTargetName) {
  absl::Status sync_status;
  hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: Target name is not specified.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierInvalidTargetName) {
  absl::Status sync_status;
  request_.target_name = "[foo.com@443";
  hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: Failed to split hostname and port.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierDNSExactCheckSucceeds) {
  absl::Status sync_status;
  request_.target_name = "foo.com:443";
  request_.peer_info.san_names.dns_names = new char*[1];
  request_.peer_info.san_names.dns_names[0] = gpr_strdup("foo.com");
  request_.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_TRUE(sync_status.ok());
}

TEST_F(GrpcTlsCertificateVerifierTest,
       HostnameVerifierDNSWildcardCheckSucceeds) {
  absl::Status sync_status;
  request_.target_name = "foo.bar.com:443";
  request_.peer_info.san_names.dns_names = new char*[1];
  request_.peer_info.san_names.dns_names[0] = gpr_strdup("*.bar.com");
  request_.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_TRUE(sync_status.ok());
}

TEST_F(GrpcTlsCertificateVerifierTest,
       HostnameVerifierDNSWildcardCaseInsensitiveCheckSucceeds) {
  absl::Status sync_status;
  request_.target_name = "fOo.bar.cOm:443";
  request_.peer_info.san_names.dns_names = new char*[1];
  request_.peer_info.san_names.dns_names[0] = gpr_strdup("*.BaR.Com");
  request_.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_TRUE(sync_status.ok());
}

TEST_F(GrpcTlsCertificateVerifierTest,
       HostnameVerifierDNSTopWildcardCheckFails) {
  absl::Status sync_status;
  request_.target_name = "foo.com:443";
  request_.peer_info.san_names.dns_names = new char*[1];
  request_.peer_info.san_names.dns_names[0] = gpr_strdup("*.");
  request_.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierDNSExactCheckFails) {
  absl::Status sync_status;
  request_.target_name = "foo.com:443";
  request_.peer_info.san_names.dns_names = new char*[1];
  request_.peer_info.san_names.dns_names[0] = gpr_strdup("bar.com");
  request_.peer_info.san_names.dns_names_size = 1;
  hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierIpCheckSucceeds) {
  absl::Status sync_status;
  request_.target_name = "192.168.0.1:443";
  request_.peer_info.san_names.ip_names = new char*[1];
  request_.peer_info.san_names.ip_names[0] = gpr_strdup("192.168.0.1");
  request_.peer_info.san_names.ip_names_size = 1;
  hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_TRUE(sync_status.ok());
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierIpCheckFails) {
  absl::Status sync_status;
  request_.target_name = "192.168.0.1:443";
  request_.peer_info.san_names.ip_names = new char*[1];
  request_.peer_info.san_names.ip_names[0] = gpr_strdup("192.168.1.1");
  request_.peer_info.san_names.ip_names_size = 1;
  hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest,
       HostnameVerifierCommonNameCheckSucceeds) {
  absl::Status sync_status;
  request_.target_name = "foo.com:443";
  request_.peer_info.common_name = gpr_strdup("foo.com");
  hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_TRUE(sync_status.ok());
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierCommonNameCheckFails) {
  absl::Status sync_status;
  request_.target_name = "foo.com:443";
  request_.peer_info.common_name = gpr_strdup("bar.com");
  hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status);
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: Hostname Verification Check failed.");
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
