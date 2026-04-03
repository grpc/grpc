//
//
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

#include "test/core/tsi/private_key_signer_test_util.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/private_key_signer.h>
#include <openssl/bio.h>
#include <openssl/digest.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/util/time.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {

#if defined(OPENSSL_IS_BORINGSSL)

namespace {
bssl::UniquePtr<EVP_PKEY> LoadPrivateKeyFromString(
    absl::string_view private_pem) {
  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(private_pem.data(), private_pem.size()));
  return bssl::UniquePtr<EVP_PKEY>(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

uint16_t GetBoringSslAlgorithm(
    PrivateKeySigner::SignatureAlgorithm signature_algorithm) {
  switch (signature_algorithm) {
    case PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha256:
      return SSL_SIGN_RSA_PKCS1_SHA256;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha384:
      return SSL_SIGN_RSA_PKCS1_SHA384;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha512:
      return SSL_SIGN_RSA_PKCS1_SHA512;
    case PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp256r1Sha256:
      return SSL_SIGN_ECDSA_SECP256R1_SHA256;
    case PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp384r1Sha384:
      return SSL_SIGN_ECDSA_SECP384R1_SHA384;
    case PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp521r1Sha512:
      return SSL_SIGN_ECDSA_SECP521R1_SHA512;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha256:
      return SSL_SIGN_RSA_PSS_RSAE_SHA256;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha384:
      return SSL_SIGN_RSA_PSS_RSAE_SHA384;
    case PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha512:
      return SSL_SIGN_RSA_PSS_RSAE_SHA512;
  }
  return -1;
}

absl::StatusOr<std::string> SignWithBoringSSL(
    absl::string_view data_to_sign,
    PrivateKeySigner::SignatureAlgorithm signature_algorithm,
    EVP_PKEY* private_key) {
  const uint8_t* in = reinterpret_cast<const uint8_t*>(data_to_sign.data());
  const size_t in_len = data_to_sign.size();

  uint16_t boring_signature_algorithm =
      GetBoringSslAlgorithm(signature_algorithm);
  if (EVP_PKEY_id(private_key) !=
      SSL_get_signature_algorithm_key_type(boring_signature_algorithm)) {
    fprintf(stderr, "Key type does not match signature algorithm.\n");
  }

  // Determine the hash.
  const EVP_MD* md =
      SSL_get_signature_algorithm_digest(boring_signature_algorithm);
  bssl::ScopedEVP_MD_CTX ctx;
  EVP_PKEY_CTX* pctx;
  if (!EVP_DigestSignInit(ctx.get(), &pctx, md, nullptr, private_key)) {
    return absl::InternalError("EVP_DigestSignInit failed");
  }

  // Configure additional signature parameters.
  if (SSL_is_signature_algorithm_rsa_pss(boring_signature_algorithm)) {
    if (!EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) ||
        !EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, -1)) {
      return absl::InternalError("EVP_PKEY_CTX failed");
    }
  }

  size_t len = 0;
  if (!EVP_DigestSign(ctx.get(), nullptr, &len, in, in_len)) {
    return absl::InternalError("EVP_DigestSign failed");
  }
  std::vector<uint8_t> private_key_result;
  private_key_result.resize(len);
  if (!EVP_DigestSign(ctx.get(), private_key_result.data(), &len, in, in_len)) {
    return absl::InternalError("EVP_DigestSign failed");
  }
  private_key_result.resize(len);
  return std::string(private_key_result.begin(), private_key_result.end());
}
}  // namespace

SyncTestPrivateKeySigner::SyncTestPrivateKeySigner(
    absl::string_view private_key, Mode mode)
    : pkey_(LoadPrivateKeyFromString(private_key)), mode_(mode) {}

std::variant<absl::StatusOr<std::string>,
             std::shared_ptr<PrivateKeySigner::AsyncSigningHandle>>
SyncTestPrivateKeySigner::Sign(absl::string_view data_to_sign,
                               SignatureAlgorithm signature_algorithm,
                               OnSignComplete /*on_sign_complete*/) {
  if (mode_ == Mode::kError) {
    return absl::InternalError("signer error");
  }
  if (mode_ == Mode::kInvalidSignature) {
    return "bad signature";
  }
  return SignWithBoringSSL(data_to_sign, signature_algorithm, pkey_.get());
}

void SyncTestPrivateKeySigner::Cancel(
    std::shared_ptr<AsyncSigningHandle> /*handle*/) {}

AsyncTestPrivateKeySigner::AsyncTestPrivateKeySigner(
    absl::string_view private_key,
    std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
        event_engine,
    Mode mode)
    : event_engine_(std::move(event_engine)),
      pkey_(LoadPrivateKeyFromString(private_key)),
      mode_(mode) {}

std::variant<absl::StatusOr<std::string>,
             std::shared_ptr<PrivateKeySigner::AsyncSigningHandle>>
AsyncTestPrivateKeySigner::Sign(absl::string_view data_to_sign,
                                SignatureAlgorithm signature_algorithm,
                                OnSignComplete on_sign_complete) {
  if (mode_ != Mode::kCancellation) {
    event_engine_->RunAfter(
        Duration::Seconds(2),
        [self = shared_from_this(), data_to_sign = std::string(data_to_sign),
         signature_algorithm,
         on_sign_complete = std::move(on_sign_complete)]() mutable {
          if (self->mode_ == Mode::kError) {
            on_sign_complete(absl::InternalError("async signer error"));
          } else {
            on_sign_complete(SignWithBoringSSL(
                data_to_sign, signature_algorithm, self->pkey_.get()));
          }
        });
  }
  return std::make_shared<AsyncSigningHandle>();
}

void AsyncTestPrivateKeySigner::Cancel(
    std::shared_ptr<AsyncSigningHandle> /*handle*/) {
  was_cancelled_.store(true);
}

bool AsyncTestPrivateKeySigner::WasCancelled() { return was_cancelled_.load(); }

#endif  // OPENSSL_IS_BORINGSSL

}  // namespace testing
}  // namespace grpc_core
