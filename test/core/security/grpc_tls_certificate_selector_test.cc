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

using ::testing::ElementsAre;
using ::testing::Ne;
using ::testing::VariantWith;

constexpr absl::string_view kTestCredsRelativePath =
    "test/core/tsi/test_creds/crl_data/";
constexpr absl::string_view kServerCertPemFile = "valid.pem";
constexpr absl::string_view kServerKeyPemFile = "valid.key";
constexpr absl::string_view kServerCertDerFile = "valid_cert.der";
constexpr absl::string_view kServerKeyDerFile = "valid_key.der";
constexpr absl::string_view kInvalidPemBlock =
    "-----BEGIN CERTIFICATE-----\ninvalid\n-----END CERTIFICATE-----";

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
  EXPECT_THAT(result->certificate_chain, ElementsAre(Ne(nullptr)));
  // Should hold an EVP_PKEY ptr.
  EXPECT_THAT(result->private_key,
              VariantWith<bssl::UniquePtr<EVP_PKEY>>(Ne(nullptr)));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemWithMultipleCertsInChainSuccess) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(
          absl::StrCat(pem_cert_chain_, pem_cert_chain_), pem_private_key_);
  ASSERT_EQ(result.status(), absl::OkStatus());
  EXPECT_THAT(result->certificate_chain, ElementsAre(Ne(nullptr), Ne(nullptr)));
  // Should hold an EVP_PKEY.
  EXPECT_THAT(result->private_key,
              VariantWith<bssl::UniquePtr<EVP_PKEY>>(Ne(nullptr)));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemWithNonsenseBeforeChainSuccess) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(
          absl::StrCat("nonsense\n", pem_cert_chain_), pem_private_key_);
  ASSERT_EQ(result.status(), absl::OkStatus());
  EXPECT_THAT(result->certificate_chain, ElementsAre(Ne(nullptr)));
  // Should hold an EVP_PKEY.
  EXPECT_THAT(result->private_key,
              VariantWith<bssl::UniquePtr<EVP_PKEY>>(Ne(nullptr)));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemWithNonsenseAfterChainSuccess) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(
          absl::StrCat(pem_cert_chain_, "\nnonsense"), pem_private_key_);
  ASSERT_EQ(result.status(), absl::OkStatus());
  EXPECT_THAT(result->certificate_chain, ElementsAre(Ne(nullptr)));
  // Should hold an EVP_PKEY.
  EXPECT_THAT(result->private_key,
              VariantWith<bssl::UniquePtr<EVP_PKEY>>(Ne(nullptr)));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemWithInvalidBlockSuccess) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(
          absl::StrCat(pem_cert_chain_, kInvalidPemBlock, pem_cert_chain_),
          pem_private_key_);
  ASSERT_EQ(result.status(), absl::OkStatus());
  // The 3rd PEM block will be ignored because of the invalid one in the middle.
  EXPECT_THAT(result->certificate_chain, ElementsAre(Ne(nullptr)));
  // Should hold an EVP_PKEY.
  EXPECT_THAT(result->private_key,
              VariantWith<bssl::UniquePtr<EVP_PKEY>>(Ne(nullptr)));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemFailedWithEmptyChain) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult("", pem_private_key_);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "The cert chain is empty.");
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemFailedWithInvalidChain) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult("invalid",
                                                         pem_private_key_);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "Failed to parse cert chain.");
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemFailedWithInvalidBlockInChain) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(kInvalidPemBlock,
                                                         pem_private_key_);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "Failed to parse cert chain.");
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemFailedWithEmptyKey) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(pem_cert_chain_, "");
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "Failed to read private key.");
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromPemFailedWithInvalidKey) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(pem_cert_chain_,
                                                         "invalid");
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "Failed to read private key.");
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromDerSuccess) {
  std::shared_ptr<PrivateKeySigner> signer;
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(der_cert_chain_,
                                                         der_private_key_);
  ASSERT_EQ(result.status(), absl::OkStatus());
  EXPECT_THAT(result->certificate_chain, ElementsAre(Ne(nullptr)));
  // Should hold an EVP_PKEY.
  EXPECT_THAT(result->private_key,
              VariantWith<bssl::UniquePtr<EVP_PKEY>>(Ne(nullptr)));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromDerWithSignerSuccess) {
  std::shared_ptr<PrivateKeySigner> signer =
      std::make_shared<TestPrivateKeySigner>();
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(der_cert_chain_,
                                                         signer);
  ASSERT_EQ(result.status(), absl::OkStatus());
  EXPECT_THAT(result->certificate_chain, ElementsAre(Ne(nullptr)));
  // Should hold a private key signer.
  EXPECT_THAT(
      result->private_key,
      VariantWith<std::shared_ptr<grpc_core::PrivateKeySigner>>(Ne(nullptr)));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromDerFailedWithEmtpyChain) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(
          std::vector<std::string>(), der_private_key_);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "The cert chain is empty.");
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertificateResultFromDerFailedWithInvalidKey) {
  absl::StatusOr<CertificateSelector::SelectCertificateResult> result =
      CertificateSelector::CreateSelectCertificateResult(der_cert_chain_,
                                                         "invalid");
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "Failed to parse private key.");
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
