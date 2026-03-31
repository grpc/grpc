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

constexpr absl::string_view kTestCredsRelativePath = "src/core/tsi/test_creds/";

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
      : cert_chain_(GetFileContents(
            absl::StrCat(kTestCredsRelativePath, "server1.pem"))),
        private_key_(GetFileContents(
            absl::StrCat(kTestCredsRelativePath, "server1.key"))) {}

  static std::vector<std::string> ConvertCertChainToDer(absl::string_view pem_cert_chain) {
    std::vector<std::string> cert_chain;
    BIO* bio = BIO_new_mem_buf(pem_cert_chain.data(), pem_cert_chain.size());
    uint8_t* cert_data = nullptr;
    long cert_len;
    while (PEM_bytes_read_bio(&cert_data, &cert_len, nullptr, PEM_STRING_X509,
                              bio, nullptr, nullptr)) {
      cert_chain.push_back(
          std::string(reinterpret_cast<char*>(cert_data), cert_len));
      OPENSSL_free(cert_data);
    }
    BIO_free(bio);
    return cert_chain;
  }

  std::string cert_chain_;
  std::string private_key_;
};

TEST_F(TlsCertificateSelectorTest, CreateSelectCertResultFromPemSuccess) {
  ASSERT_OK(
      CertificateSelector::CreateSelectCertResult(cert_chain_, private_key_));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertResultFromPemFailedWithEmptyChain) {
  ASSERT_THAT(CertificateSelector::CreateSelectCertResult("", private_key_),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertResultFromPemFailedWithInvalidChain) {
  ASSERT_THAT(
      CertificateSelector::CreateSelectCertResult("invalid", private_key_),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertResultFromPemFailedWithEmptyKey) {
  ASSERT_THAT(CertificateSelector::CreateSelectCertResult(cert_chain_, ""),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(TlsCertificateSelectorTest,
       CreateSelectCertResultFromPemFailedWithInvalidKey) {
  ASSERT_THAT(
      CertificateSelector::CreateSelectCertResult(cert_chain_, "invalid"),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(TlsCertificateSelectorTest, CreateSelectCertResultFromDerSuccess) {
  ASSERT_OK(CertificateSelector::CreateSelectCertResult(
      ConvertCertChainToDer(cert_chain_), /*private_key_signer=*/nullptr));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
