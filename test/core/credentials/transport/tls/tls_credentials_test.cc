//
// Copyright 2025 gRPC authors.
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

#include "src/core/credentials/transport/tls/tls_credentials.h"

#include <grpc/grpc_security.h>
#include <gtest/gtest.h>

#include "gmock/gmock.h"
#include "src/core/credentials/transport/tls/grpc_tls_credentials_options.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(TlsCredentialsTest, CompareSuccess) {
  auto* tls_creds_1 =
      grpc_tls_credentials_create(grpc_tls_credentials_options_create());
  auto* tls_creds_2 =
      grpc_tls_credentials_create(grpc_tls_credentials_options_create());
  EXPECT_EQ(tls_creds_1->cmp(tls_creds_2), 0);
  EXPECT_EQ(tls_creds_2->cmp(tls_creds_1), 0);
  grpc_channel_credentials_release(tls_creds_1);
  grpc_channel_credentials_release(tls_creds_2);
}

TEST(TlsCredentialsTest, WithVerifierCompareSuccess) {
  auto* options_1 = grpc_tls_credentials_options_create();
  options_1->set_certificate_verifier(
      MakeRefCounted<HostNameCertificateVerifier>());
  auto* tls_creds_1 = grpc_tls_credentials_create(options_1);
  auto* options_2 = grpc_tls_credentials_options_create();
  options_2->set_certificate_verifier(
      MakeRefCounted<HostNameCertificateVerifier>());
  auto* tls_creds_2 = grpc_tls_credentials_create(options_2);
  EXPECT_EQ(tls_creds_1->cmp(tls_creds_2), 0);
  EXPECT_EQ(tls_creds_2->cmp(tls_creds_1), 0);
  grpc_channel_credentials_release(tls_creds_1);
  grpc_channel_credentials_release(tls_creds_2);
}

TEST(TlsCredentialsTest, CompareFailure) {
  auto* options_1 = grpc_tls_credentials_options_create();
  options_1->set_check_call_host(true);
  auto* tls_creds_1 = grpc_tls_credentials_create(options_1);
  auto* options_2 = grpc_tls_credentials_options_create();
  options_2->set_check_call_host(false);
  auto* tls_creds_2 = grpc_tls_credentials_create(options_2);
  EXPECT_NE(tls_creds_1->cmp(tls_creds_2), 0);
  EXPECT_NE(tls_creds_2->cmp(tls_creds_1), 0);
  grpc_channel_credentials_release(tls_creds_1);
  grpc_channel_credentials_release(tls_creds_2);
}

TEST(TlsCredentialsTest, WithVerifierCompareFailure) {
  auto* options_1 = grpc_tls_credentials_options_create();
  options_1->set_certificate_verifier(
      MakeRefCounted<HostNameCertificateVerifier>());
  auto* tls_creds_1 = grpc_tls_credentials_create(options_1);
  auto* options_2 = grpc_tls_credentials_options_create();
  grpc_tls_certificate_verifier_external verifier = {nullptr, nullptr, nullptr,
                                                     nullptr};
  options_2->set_certificate_verifier(
      MakeRefCounted<ExternalCertificateVerifier>(&verifier));
  auto* tls_creds_2 = grpc_tls_credentials_create(options_2);
  EXPECT_NE(tls_creds_1->cmp(tls_creds_2), 0);
  EXPECT_NE(tls_creds_2->cmp(tls_creds_1), 0);
  grpc_channel_credentials_release(tls_creds_1);
  grpc_channel_credentials_release(tls_creds_2);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  return result;
}
