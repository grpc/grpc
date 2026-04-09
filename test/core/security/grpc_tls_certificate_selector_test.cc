// Copyright 2026 gRPC authors.
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

#include "src/core/credentials/transport/tls/grpc_tls_certificate_selector.h"

#include <grpc/private_key_signer.h>
#include <openssl/bio.h>
#include <openssl/pem.h>

#include <string>

#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

constexpr absl::string_view kTestCredsRelativePath =
    "test/core/tsi/test_creds/crl_data/";
constexpr absl::string_view kServerCertPemFile = "valid.pem";
constexpr absl::string_view kServerKeyPemFile = "valid.key";
constexpr absl::string_view kServerCertDerFile = "valid_cert.der";
constexpr absl::string_view kServerKeyDerFile = "valid_key.der";
constexpr absl::string_view kInvalidPemBlock =
    "-----BEGIN CERTIFICATE-----\ninvalid\n-----END CERTIFICATE-----";

template <typename T>
absl::Status GetStatus(const absl::StatusOr<T>& s) {
  return s.status();
}

MATCHER_P(StatusIs, status,
          absl::StrCat(".status() is ", ::testing::PrintToString(status))) {
  return GetStatus(arg).code() == status;
}

#define ASSERT_OK(x) ASSERT_THAT(x, StatusIs(absl::StatusCode::kOk))

class TlsCertificateSelectorTest : public ::testing::Test {
 protected:
  TlsCertificateSelectorTest()
      : pem_cert_chain_(GetFileContents(
            absl::StrCat(kTestCredsRelativePath, kServerCertPemFile))),
        pem_private_key_(GetFileContents(
            absl::StrCat(kTestCredsRelativePath, kServerKeyPemFile))),
        der_cert_chain_({GetFileContents(
            absl::StrCat(kTestCredsRelativePath, kServerCertDerFile))}),
        der_private_key_(GetFileContents(
            absl::StrCat(kTestCredsRelativePath, kServerKeyDerFile))) {}

  std::string pem_cert_chain_;
  std::string pem_private_key_;
  std::vector<std::string> der_cert_chain_;
  std::string der_private_key_;
};

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemSuccess) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(pem_cert_chain_,
                                                         pem_private_key_);
  ASSERT_OK(result);
  EXPECT_EQ(result->cert_chain.size(), 1);
  // Should hold an EVP_PKEY ptr.
  EXPECT_EQ(result->private_key.index(), 0);
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemWithMultipleChainsSuccess) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(
          absl::StrCat(pem_cert_chain_, pem_cert_chain_), pem_private_key_);
  ASSERT_OK(result);
  EXPECT_EQ(result->cert_chain.size(), 2);
  // Should hold an EVP_PKEY.
  EXPECT_EQ(result->private_key.index(), 0);
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemFailedWithEmptyChain) {
  ASSERT_THAT(
      CertificateSelector::CreateSelectCertificateResult("", pem_private_key_),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemFailedWithInvalidChain) {
  ASSERT_THAT(CertificateSelector::CreateSelectCertificateResult(
                  "invalid", pem_private_key_),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemFailedWithInvalidBlockInChain) {
  ASSERT_THAT(CertificateSelector::CreateSelectCertificateResult(
                  kInvalidPemBlock, pem_private_key_),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemFailedWithEmptyKey) {
  ASSERT_THAT(
      CertificateSelector::CreateSelectCertificateResult(pem_cert_chain_, ""),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemFailedWithInvalidKey) {
  ASSERT_THAT(CertificateSelector::CreateSelectCertificateResult(
                  pem_cert_chain_, "invalid"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromDerSuccess) {
  std::shared_ptr<PrivateKeySigner> signer;
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(der_cert_chain_,
                                                         der_private_key_);
  std::cerr << result.status();
  ASSERT_OK(result);
  EXPECT_FALSE(result->cert_chain.empty());
  // Should hold an EVP_PKEY.
  EXPECT_EQ(result->private_key.index(), 0);
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromDerWithSignerSuccess) {
  std::shared_ptr<PrivateKeySigner> signer;
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(der_cert_chain_,
                                                         signer);
  ASSERT_OK(result);
  std::cerr << result.status().message() << std::endl;
  EXPECT_FALSE(result->cert_chain.empty());
  // Should hold a private key signer.
  EXPECT_EQ(result->private_key.index(), 1);
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromDerFailedWithEmtpyChain) {
  ASSERT_THAT(CertificateSelector::CreateSelectCertificateResult(
                  std::vector<std::string>(), der_private_key_),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromDerFailedWithInvalidKey) {
  ASSERT_THAT(CertificateSelector::CreateSelectCertificateResult(
                  der_cert_chain_, "invalid"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
