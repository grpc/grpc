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

#include "src/core/credentials/transport/tls/grpc_tls_certificate_verifier.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <gtest/gtest.h>

#include <deque>
#include <list>

#include "absl/log/log.h"
#include "gmock/gmock.h"
#include "src/core/credentials/transport/tls/tls_security_connector.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/crash.h"
#include "src/core/util/tmpfile.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"

namespace grpc_core {

namespace testing {

// Unit tests for grpc_tls_certificate_verifier and all its successors.
// In these tests, |request_| is not outliving the test itself, so it's fine to
// point fields in |request_| directly to the address of local variables. In
// actual implementation, these fields are dynamically allocated.
class GrpcTlsCertificateVerifierTest : public ::testing::Test {
 protected:
  void SetUp() override { memset(&request_, 0, sizeof(request_)); }

  void TearDown() override {}

  grpc_tls_custom_verification_check_request request_;
  NoOpCertificateVerifier no_op_certificate_verifier_;
  HostNameCertificateVerifier hostname_certificate_verifier_;
};

TEST_F(GrpcTlsCertificateVerifierTest, SyncExternalVerifierSucceeds) {
  auto* sync_verifier = new SyncExternalVerifier(true);
  ExternalCertificateVerifier core_external_verifier(sync_verifier->base());
  absl::Status sync_status;
  EXPECT_TRUE(core_external_verifier.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_TRUE(sync_status.ok())
      << sync_status.code() << " " << sync_status.message();
}

TEST_F(GrpcTlsCertificateVerifierTest, SyncExternalVerifierFails) {
  auto* sync_verifier = new SyncExternalVerifier(false);
  ExternalCertificateVerifier core_external_verifier(sync_verifier->base());
  absl::Status sync_status;
  EXPECT_TRUE(core_external_verifier.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: SyncExternalVerifier failed");
}

TEST_F(GrpcTlsCertificateVerifierTest, AsyncExternalVerifierSucceeds) {
  absl::Status sync_status;
  // This is to make sure the callback has already been completed before we
  // destroy ExternalCertificateVerifier object.
  gpr_event callback_completed_event;
  gpr_event_init(&callback_completed_event);
  auto* async_verifier = new AsyncExternalVerifier(true);
  ExternalCertificateVerifier core_external_verifier(async_verifier->base());
  EXPECT_FALSE(core_external_verifier.Verify(
      &request_,
      [&callback_completed_event](absl::Status async_status) {
        EXPECT_TRUE(async_status.ok())
            << async_status.code() << " " << async_status.message();
        gpr_event_set(&callback_completed_event, reinterpret_cast<void*>(1));
      },
      &sync_status));
  void* callback_completed =
      gpr_event_wait(&callback_completed_event,
                     gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                  gpr_time_from_seconds(10, GPR_TIMESPAN)));
  EXPECT_NE(callback_completed, nullptr);
}

TEST_F(GrpcTlsCertificateVerifierTest, AsyncExternalVerifierFails) {
  absl::Status sync_status;
  // This is to make sure the callback has already been completed before we
  // destroy ExternalCertificateVerifier object.
  gpr_event callback_completed_event;
  gpr_event_init(&callback_completed_event);
  auto* async_verifier = new AsyncExternalVerifier(false);
  ExternalCertificateVerifier core_external_verifier(async_verifier->base());
  EXPECT_FALSE(core_external_verifier.Verify(
      &request_,
      [&callback_completed_event](absl::Status async_status) {
        LOG(INFO) << "Callback is invoked.";
        EXPECT_EQ(async_status.code(), absl::StatusCode::kUnauthenticated);
        EXPECT_EQ(async_status.ToString(),
                  "UNAUTHENTICATED: AsyncExternalVerifier failed");
        gpr_event_set(&callback_completed_event, reinterpret_cast<void*>(1));
      },
      &sync_status));
  void* callback_completed =
      gpr_event_wait(&callback_completed_event,
                     gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                  gpr_time_from_seconds(10, GPR_TIMESPAN)));
  EXPECT_NE(callback_completed, nullptr);
}

TEST_F(GrpcTlsCertificateVerifierTest, NoOpCertificateVerifierSucceeds) {
  absl::Status sync_status;
  EXPECT_TRUE(no_op_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_TRUE(sync_status.ok())
      << sync_status.code() << " " << sync_status.message();
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierNullTargetName) {
  absl::Status sync_status;
  EXPECT_TRUE(hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: Target name is not specified.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierInvalidTargetName) {
  absl::Status sync_status;
  request_.target_name = "[foo.com@443";
  EXPECT_TRUE(hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: Failed to split hostname and port.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierDNSExactCheckSucceeds) {
  absl::Status sync_status;
  request_.target_name = "foo.com:443";
  char* dns_names[] = {const_cast<char*>("foo.com")};
  request_.peer_info.san_names.dns_names = dns_names;
  request_.peer_info.san_names.dns_names_size = 1;
  EXPECT_TRUE(hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_TRUE(sync_status.ok())
      << sync_status.code() << " " << sync_status.message();
}

TEST_F(GrpcTlsCertificateVerifierTest,
       HostnameVerifierDNSWildcardCheckSucceeds) {
  absl::Status sync_status;
  request_.target_name = "foo.bar.com:443";
  char* dns_names[] = {const_cast<char*>("*.bar.com")};
  request_.peer_info.san_names.dns_names = dns_names;
  request_.peer_info.san_names.dns_names_size = 1;
  EXPECT_TRUE(hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_TRUE(sync_status.ok())
      << sync_status.code() << " " << sync_status.message();
}

TEST_F(GrpcTlsCertificateVerifierTest,
       HostnameVerifierDNSWildcardCaseInsensitiveCheckSucceeds) {
  absl::Status sync_status;
  request_.target_name = "fOo.bar.cOm:443";
  char* dns_names[] = {const_cast<char*>("*.BaR.Com")};
  request_.peer_info.san_names.dns_names = dns_names;
  request_.peer_info.san_names.dns_names_size = 1;
  EXPECT_TRUE(hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_TRUE(sync_status.ok())
      << sync_status.code() << " " << sync_status.message();
}

TEST_F(GrpcTlsCertificateVerifierTest,
       HostnameVerifierDNSTopWildcardCheckFails) {
  absl::Status sync_status;
  request_.target_name = "foo.com:443";
  char* dns_names[] = {const_cast<char*>("*.")};
  request_.peer_info.san_names.dns_names = dns_names;
  request_.peer_info.san_names.dns_names_size = 1;
  EXPECT_TRUE(hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierDNSExactCheckFails) {
  absl::Status sync_status;
  request_.target_name = "foo.com:443";
  char* dns_names[] = {const_cast<char*>("bar.com")};
  request_.peer_info.san_names.dns_names = dns_names;
  request_.peer_info.san_names.dns_names_size = 1;
  EXPECT_TRUE(hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierIpCheckSucceeds) {
  absl::Status sync_status;
  request_.target_name = "192.168.0.1:443";
  char* ip_names[] = {const_cast<char*>("192.168.0.1")};
  request_.peer_info.san_names.ip_names = ip_names;
  request_.peer_info.san_names.ip_names_size = 1;
  EXPECT_TRUE(hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_TRUE(sync_status.ok())
      << sync_status.code() << " " << sync_status.message();
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierIpCheckFails) {
  absl::Status sync_status;
  request_.target_name = "192.168.0.1:443";
  char* ip_names[] = {const_cast<char*>("192.168.1.1")};
  request_.peer_info.san_names.ip_names = ip_names;
  request_.peer_info.san_names.ip_names_size = 1;
  EXPECT_TRUE(hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest,
       HostnameVerifierCommonNameCheckSucceeds) {
  absl::Status sync_status;
  request_.target_name = "foo.com:443";
  request_.peer_info.common_name = "foo.com";
  EXPECT_TRUE(hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_TRUE(sync_status.ok())
      << sync_status.code() << " " << sync_status.message();
}

TEST_F(GrpcTlsCertificateVerifierTest, HostnameVerifierCommonNameCheckFails) {
  absl::Status sync_status;
  request_.target_name = "foo.com:443";
  request_.peer_info.common_name = "bar.com";
  EXPECT_TRUE(hostname_certificate_verifier_.Verify(
      &request_, [](absl::Status) {}, &sync_status));
  EXPECT_EQ(sync_status.code(), absl::StatusCode::kUnauthenticated);
  EXPECT_EQ(sync_status.ToString(),
            "UNAUTHENTICATED: Hostname Verification Check failed.");
}

TEST_F(GrpcTlsCertificateVerifierTest, ComparingDifferentObjectTypesFails) {
  grpc_tls_certificate_verifier_external verifier = {nullptr, nullptr, nullptr,
                                                     nullptr};
  ExternalCertificateVerifier external_verifier(&verifier);
  HostNameCertificateVerifier hostname_certificate_verifier;
  EXPECT_NE(external_verifier.Compare(&hostname_certificate_verifier), 0);
  EXPECT_NE(hostname_certificate_verifier.Compare(&external_verifier), 0);
}

TEST_F(GrpcTlsCertificateVerifierTest, HostNameCertificateVerifier) {
  HostNameCertificateVerifier hostname_certificate_verifier_1;
  HostNameCertificateVerifier hostname_certificate_verifier_2;
  EXPECT_EQ(
      hostname_certificate_verifier_1.Compare(&hostname_certificate_verifier_2),
      0);
  EXPECT_EQ(
      hostname_certificate_verifier_2.Compare(&hostname_certificate_verifier_1),
      0);
}

TEST_F(GrpcTlsCertificateVerifierTest, ExternalCertificateVerifierSuccess) {
  grpc_tls_certificate_verifier_external verifier = {nullptr, nullptr, nullptr,
                                                     nullptr};
  ExternalCertificateVerifier external_verifier_1(&verifier);
  ExternalCertificateVerifier external_verifier_2(&verifier);
  EXPECT_EQ(external_verifier_1.Compare(&external_verifier_2), 0);
  EXPECT_EQ(external_verifier_2.Compare(&external_verifier_1), 0);
}

TEST_F(GrpcTlsCertificateVerifierTest, ExternalCertificateVerifierFailure) {
  grpc_tls_certificate_verifier_external verifier_1 = {nullptr, nullptr,
                                                       nullptr, nullptr};
  ExternalCertificateVerifier external_verifier_1(&verifier_1);
  grpc_tls_certificate_verifier_external verifier_2 = {nullptr, nullptr,
                                                       nullptr, nullptr};
  ExternalCertificateVerifier external_verifier_2(&verifier_2);
  EXPECT_NE(external_verifier_1.Compare(&external_verifier_2), 0);
  EXPECT_NE(external_verifier_2.Compare(&external_verifier_1), 0);
}

}  // namespace testing

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
