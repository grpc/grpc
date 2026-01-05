//
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
//

#include <grpc/private_key_signer.h>

#include <string>
#include <utility>

#include "openssl/bio.h"
#include "openssl/crypto.h"
#include "openssl/evp.h"
#include "openssl/pem.h"
#include "openssl/ssl.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/private_key_signer_py_wrapper.h"
#include "src/core/util/load_file.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"

namespace grpc_core {
namespace testing {
namespace {

bssl::UniquePtr<EVP_PKEY> LoadPrivateKeyFromString(
    absl::string_view private_pem) {
  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(private_pem.data(), private_pem.size()));
  return bssl::UniquePtr<EVP_PKEY>(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

bssl::UniquePtr<EVP_PKEY> LoadPublicKeyFromString(
    absl::string_view public_pem) {
  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(public_pem.data(), public_pem.size()));
  bssl::UniquePtr<X509> x509(
      PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  if (!x509) return nullptr;
  return bssl::UniquePtr<EVP_PKEY>(X509_get_pubkey(x509.get()));
}

bool GetBoringSslAlgorithm(
    PrivateKeySigner::SignatureAlgorithm signature_algorithm, const EVP_MD** md,
    int* padding) {
  switch (signature_algorithm) {
    case PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha256:
      *md = EVP_sha256();
      *padding = RSA_PKCS1_PADDING;
      return true;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha384:
      *md = EVP_sha384();
      *padding = RSA_PKCS1_PADDING;
      return true;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha512:
      *md = EVP_sha512();
      *padding = RSA_PKCS1_PADDING;
      return true;
    case PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp256r1Sha256:
      *md = EVP_sha256();
      *padding = 0;
      return true;
    case PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp384r1Sha384:
      *md = EVP_sha384();
      *padding = 0;
      return true;
    case PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp521r1Sha512:
      *md = EVP_sha512();
      *padding = 0;
      return true;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha256:
      *md = EVP_sha256();
      *padding = RSA_PKCS1_PSS_PADDING;
      return true;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha384:
      *md = EVP_sha384();
      *padding = RSA_PKCS1_PSS_PADDING;
      return true;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha512:
      *md = EVP_sha512();
      *padding = RSA_PKCS1_PSS_PADDING;
      return true;
    default:
      return false;
  }
}

struct SignerData {
  bssl::UniquePtr<EVP_PKEY> pkey;
};

void SignPyWrapperImpl(
    absl::string_view data_to_sign,
    grpc_core::PrivateKeySigner::SignatureAlgorithm signature_algorithm,
    OnSignCompletePyWrapper on_sign_complete_py_wrapper, void* completion_data,
    void* user_data) {
  SignerData* signer_data = static_cast<SignerData*>(user_data);
  const EVP_MD* md = nullptr;
  int padding = 0;
  if (!GetBoringSslAlgorithm(signature_algorithm, &md, &padding)) {
    on_sign_complete_py_wrapper(
        absl::InternalError("Unsupported signature algorithm"), completion_data);
    return;
  }
  bssl::ScopedEVP_MD_CTX ctx;
  EVP_PKEY_CTX* pctx = nullptr;
  if (!EVP_DigestSignInit(ctx.get(), &pctx, md, nullptr,
                          signer_data->pkey.get())) {
    on_sign_complete_py_wrapper(absl::InternalError("EVP_DigestSignInit failed"),
                                completion_data);
    return;
  }
  if (padding == RSA_PKCS1_PADDING) {
    if (!EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PADDING)) {
      on_sign_complete_py_wrapper(
          absl::InternalError("EVP_PKEY_CTX_set_rsa_padding failed"),
          completion_data);
      return;
    }
  } else if (padding == RSA_PKCS1_PSS_PADDING) {
    if (!EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) ||
        !EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1)) {
      on_sign_complete_py_wrapper(
          absl::InternalError("EVP_PKEY_CTX_set_rsa_padding failed"),
          completion_data);
      return;
    }
  }
  size_t sig_len = 0;
  if (!EVP_DigestSignUpdate(ctx.get(), data_to_sign.data(),
                          data_to_sign.size()) ||
      !EVP_DigestSignFinal(ctx.get(), nullptr, &sig_len)) {
    on_sign_complete_py_wrapper(absl::InternalError("EVP_DigestSignFinal failed"),
                                completion_data);
    return;
  }
  std::string sig;
  sig.resize(sig_len);
  if (!EVP_DigestSignFinal(ctx.get(), reinterpret_cast<uint8_t*>(sig.data()),
                           &sig_len)) {
    on_sign_complete_py_wrapper(absl::InternalError("EVP_DigestSignFinal failed"),
                                completion_data);
    return;
  }
  sig.resize(sig_len);
  on_sign_complete_py_wrapper(std::move(sig), completion_data);
}

absl::Status Verify(EVP_PKEY* pkey, PrivateKeySigner::SignatureAlgorithm alg,
                    absl::string_view data, absl::string_view sig) {
  const EVP_MD* md = nullptr;
  int padding = 0;
  if (!GetBoringSslAlgorithm(alg, &md, &padding)) {
    return absl::InternalError("Unsupported signature algorithm");
  }
  bssl::ScopedEVP_MD_CTX ctx;
  EVP_PKEY_CTX* pctx = nullptr;
  if (!EVP_DigestVerifyInit(ctx.get(), &pctx, md, nullptr, pkey)) {
    return absl::InternalError("EVP_DigestVerifyInit failed");
  }
  if (padding == RSA_PKCS1_PADDING) {
    if (!EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PADDING)) {
      return absl::InternalError("EVP_PKEY_CTX_set_rsa_padding failed");
    }
  } else if (padding == RSA_PKCS1_PSS_PADDING) {
    if (!EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) ||
        !EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1)) {
      return absl::InternalError("EVP_PKEY_CTX_set_rsa_padding failed");
    }
  }
  if (!EVP_DigestVerifyUpdate(ctx.get(), data.data(), data.size())) {
    return absl::InternalError("EVP_DigestVerifyUpdate failed");
  }
  if (EVP_DigestVerifyFinal(ctx.get(), reinterpret_cast<const uint8_t*>(sig.data()),
                          sig.size())) {
    return absl::OkStatus();
  }
  return absl::InternalError("Signature verification failed");
}

struct TestVector {
  std::string name;
  std::string key_path;
  std::string cert_path;
  PrivateKeySigner::SignatureAlgorithm alg;
};

class PyWrapperPrivateKeySignerTest
    : public ::testing::TestWithParam<TestVector> {};

TEST_P(PyWrapperPrivateKeySignerTest, SignAndVerify) {
  const TestVector& param = GetParam();
  auto key_slice = LoadFile(param.key_path, 0);
  ASSERT_TRUE(key_slice.ok()) << key_slice.status();
  std::string key_str(key_slice->as_string_view());
  bssl::UniquePtr<EVP_PKEY> key = LoadPrivateKeyFromString(key_str);
  ASSERT_NE(key, nullptr);

  auto cert_slice = LoadFile(param.cert_path, 0);
  ASSERT_TRUE(cert_slice.ok());
  std::string cert_str(cert_slice->as_string_view());
  bssl::UniquePtr<EVP_PKEY> pub_key = LoadPublicKeyFromString(cert_str);
  ASSERT_NE(pub_key, nullptr);

  SignerData signer_data;
  signer_data.pkey = std::move(key);

  std::unique_ptr<PrivateKeySigner> signer(
      BuildPrivateKeySigner(SignPyWrapperImpl, &signer_data));

  absl::StatusOr<std::string> result;
  absl::Notification notification;
  signer->Sign("Hello World!", param.alg,
               [&](absl::StatusOr<std::string> sign_result) {
                 result = std::move(sign_result);
                 notification.Notify();
               });
  notification.WaitForNotification();
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(Verify(pub_key.get(), param.alg, "Hello World!", *result).ok());
}

INSTANTIATE_TEST_SUITE_P(
    PyWrapperPrivateKeySignerTest, PyWrapperPrivateKeySignerTest,
    ::testing::Values(TestVector{
        "RsaPkcs1Sha256", "test/core/tsi/test_creds/spiffe_end2end/ca.key",
        "test/core/tsi/test_creds/spiffe_end2end/ca.pem",
        PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha256}),
    [](const ::testing::TestParamInfo<PyWrapperPrivateKeySignerTest::ParamType>&
           info) { return info.param.name; });

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
