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
//
//

#include "test/cpp/end2end/tls_test_private_key_signer.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpcpp/security/tls_private_key_signer.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/util/down_cast.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

#if defined(OPENSSL_IS_BORINGSSL)

#include <openssl/digest.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>

namespace grpc {
namespace testing {
namespace {

using AsyncSigningHandle = grpc_core::PrivateKeySigner::AsyncSigningHandle;
using OnSignComplete = grpc_core::PrivateKeySigner::OnSignComplete;
using SignatureAlgorithm = grpc_core::PrivateKeySigner::SignatureAlgorithm;

bssl::UniquePtr<EVP_PKEY> LoadPrivateKeyFromString(
    absl::string_view private_pem) {
  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(private_pem.data(), private_pem.size()));
  return bssl::UniquePtr<EVP_PKEY>(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

uint16_t GetBoringSslAlgorithm(
    grpc::experimental::PrivateKeySigner::SignatureAlgorithm
        signature_algorithm) {
  switch (signature_algorithm) {
    case grpc::experimental::PrivateKeySigner::SignatureAlgorithm::
        kRsaPkcs1Sha256:
      return SSL_SIGN_RSA_PKCS1_SHA256;
    case grpc::experimental::PrivateKeySigner::SignatureAlgorithm::
        kRsaPkcs1Sha384:
      return SSL_SIGN_RSA_PKCS1_SHA384;
    case grpc::experimental::PrivateKeySigner::SignatureAlgorithm::
        kRsaPkcs1Sha512:
      return SSL_SIGN_RSA_PKCS1_SHA512;
    case grpc::experimental::PrivateKeySigner::SignatureAlgorithm::
        kEcdsaSecp256r1Sha256:
      return SSL_SIGN_ECDSA_SECP256R1_SHA256;
    case grpc::experimental::PrivateKeySigner::SignatureAlgorithm::
        kEcdsaSecp384r1Sha384:
      return SSL_SIGN_ECDSA_SECP384R1_SHA384;
    case grpc::experimental::PrivateKeySigner::SignatureAlgorithm::
        kEcdsaSecp521r1Sha512:
      return SSL_SIGN_ECDSA_SECP521R1_SHA512;
    case grpc::experimental::PrivateKeySigner::SignatureAlgorithm::
        kRsaPssRsaeSha256:
      return SSL_SIGN_RSA_PSS_RSAE_SHA256;
    case grpc::experimental::PrivateKeySigner::SignatureAlgorithm::
        kRsaPssRsaeSha384:
      return SSL_SIGN_RSA_PSS_RSAE_SHA384;
    case grpc::experimental::PrivateKeySigner::SignatureAlgorithm::
        kRsaPssRsaeSha512:
      return SSL_SIGN_RSA_PSS_RSAE_SHA512;
  }
  return -1;
}

absl::StatusOr<std::string> SignWithBoringSSL(
    absl::string_view data_to_sign,
    grpc::experimental::PrivateKeySigner::SignatureAlgorithm
        signature_algorithm,
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

std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
SyncTestPrivateKeySigner::Sign(absl::string_view data_to_sign,
                               SignatureAlgorithm signature_algorithm,
                               OnSignComplete /*on_sign_complete*/) {
  if (mode_ == Mode::kError) {
    return absl::InternalError("Test error sync");
  }
  return SignWithBoringSSL(data_to_sign, signature_algorithm, pkey_.get());
}

AsyncTestPrivateKeySigner::AsyncTestPrivateKeySigner(
    absl::string_view private_key, Mode mode, absl::Duration delay)
    : pkey_(LoadPrivateKeyFromString(private_key)),
      mode_(mode),
      delay_(delay) {}

std::variant<absl::StatusOr<std::string>,
             std::shared_ptr<grpc_core::PrivateKeySigner::AsyncSigningHandle>>
AsyncTestPrivateKeySigner::Sign(absl::string_view data_to_sign,
                                SignatureAlgorithm signature_algorithm,
                                OnSignComplete on_sign_complete) {
  auto event_engine = grpc_event_engine::experimental::GetDefaultEventEngine();
  auto handle = std::make_shared<AsyncSigningHandleInternal>();

  if (mode_ == Mode::kCancellation) {
    event_engine->Run([self = shared_from_this()]() {
      while (!self->was_cancelled_.load()) {
        absl::SleepFor(absl::Milliseconds(10));
      }
    });
  } else {
    handle->task_handle = event_engine->RunAfter(
        std::chrono::nanoseconds(absl::ToInt64Nanoseconds(delay_)),
        [self = shared_from_this(), data_to_sign = std::string(data_to_sign),
         signature_algorithm,
         on_sign_complete = std::move(on_sign_complete)]() mutable {
          if (self->mode_ == Mode::kError) {
            on_sign_complete(absl::InternalError("Test error async"));
          } else {
            on_sign_complete(SignWithBoringSSL(
                data_to_sign, signature_algorithm, self->pkey_.get()));
          }
        });
  }
  return handle;
}

void AsyncTestPrivateKeySigner::Cancel(
    std::shared_ptr<grpc_core::PrivateKeySigner::AsyncSigningHandle> handle) {
  if (mode_ == Mode::kDelayed) {
    auto event_engine =
        grpc_event_engine::experimental::GetDefaultEventEngine();
    auto* internal_handle =
        grpc_core::DownCast<AsyncSigningHandleInternal*>(handle.get());
    event_engine->Cancel(internal_handle->task_handle);
  }
}

}  // namespace testing
}  // namespace grpc

#endif  // OPENSSL_IS_BORINGSSL
