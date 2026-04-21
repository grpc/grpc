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

#if defined(OPENSSL_IS_BORINGSSL)

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

class TestPrivateKeySigner final : public PrivateKeySigner {
 public:
  std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
  Sign(absl::string_view /*data_to_sign*/,
       SignatureAlgorithm /*signature_algorithm*/,
       OnSignComplete /*on_sign_complete*/) override {
    return absl::UnimplementedError("unsupported");
  };

  void Cancel(std::shared_ptr<AsyncSigningHandle> /*handle*/) override{};
};

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
  ASSERT_EQ(result.status(), absl::OkStatus());
  EXPECT_EQ(result->certificate_chain.size(), 1);
  EXPECT_NE(result->certificate_chain[0], nullptr);
  // Should hold an EVP_PKEY ptr.
  EXPECT_EQ(result->private_key.index(), 0);
  EXPECT_NE(std::get<bssl::UniquePtr<EVP_PKEY>>(result->private_key), nullptr);
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemWithMultipleCertsInChainSuccess) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(
          absl::StrCat(pem_cert_chain_, pem_cert_chain_), pem_private_key_);
  ASSERT_EQ(result.status(), absl::OkStatus());
  EXPECT_EQ(result->certificate_chain.size(), 2);
  EXPECT_NE(result->certificate_chain[0], nullptr);
  EXPECT_NE(result->certificate_chain[1], nullptr);
  // Should hold an EVP_PKEY.
  EXPECT_EQ(result->private_key.index(), 0);
  EXPECT_NE(std::get<bssl::UniquePtr<EVP_PKEY>>(result->private_key), nullptr);
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemWithNonsenseBeforeChainSuccess) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(
          absl::StrCat("nonsense\n", pem_cert_chain_), pem_private_key_);
  ASSERT_EQ(result.status(), absl::OkStatus());
  EXPECT_EQ(result->certificate_chain.size(), 1);
  EXPECT_NE(result->certificate_chain[0], nullptr);
  // Should hold an EVP_PKEY.
  EXPECT_EQ(result->private_key.index(), 0);
  EXPECT_NE(std::get<bssl::UniquePtr<EVP_PKEY>>(result->private_key), nullptr);
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemWithNonsenseAfterChainSuccess) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(
          absl::StrCat(pem_cert_chain_, "\nnonsense"), pem_private_key_);
  ASSERT_EQ(result.status(), absl::OkStatus());
  EXPECT_EQ(result->certificate_chain.size(), 1);
  EXPECT_NE(result->certificate_chain[0], nullptr);
  // Should hold an EVP_PKEY.
  EXPECT_EQ(result->private_key.index(), 0);
  EXPECT_NE(std::get<bssl::UniquePtr<EVP_PKEY>>(result->private_key), nullptr);
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemWithInvalidBlockSuccess) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(
          absl::StrCat(pem_cert_chain_, kInvalidPemBlock, pem_cert_chain_),
          pem_private_key_);
  ASSERT_EQ(result.status(), absl::OkStatus());
  // The 3rd PEM block will be ignored because of the invalid one in the middle.
  EXPECT_EQ(result->certificate_chain.size(), 1);
  EXPECT_NE(result->certificate_chain[0], nullptr);
  // Should hold an EVP_PKEY.
  EXPECT_EQ(result->private_key.index(), 0);
  EXPECT_NE(std::get<bssl::UniquePtr<EVP_PKEY>>(result->private_key), nullptr);
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
  ASSERT_EQ(result.status(), absl::OkStatus());
  EXPECT_EQ(result->certificate_chain.size(), 1);
  EXPECT_NE(result->certificate_chain[0], nullptr);
  // Should hold an EVP_PKEY.
  EXPECT_EQ(result->private_key.index(), 0);
  EXPECT_NE(std::get<bssl::UniquePtr<EVP_PKEY>>(result->private_key), nullptr);
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromDerWithSignerSuccess) {
  std::shared_ptr<PrivateKeySigner> signer =
      std::make_shared<TestPrivateKeySigner>();
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(der_cert_chain_,
                                                         signer);
  ASSERT_EQ(result.status(), absl::OkStatus());
  EXPECT_EQ(result->certificate_chain.size(), 1);
  EXPECT_NE(result->certificate_chain[0], nullptr);
  // Should hold a private key signer.
  EXPECT_EQ(result->private_key.index(), 1);
  EXPECT_NE(std::get<std::shared_ptr<PrivateKeySigner>>(result->private_key),
            nullptr);
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

#endif  // OPENSSL_IS_BORINGSSL

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
