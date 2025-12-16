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

#include "src/core/tsi/private_key_offload_util.h"

#include <openssl/ssl.h>

#include <cstdint>
#include <string>
#include <utility>

#include "src/core/tsi/transport_security_interface.h"
#include "absl/functional/bind_front.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

absl::StatusOr<CustomPrivateKeySigner::SignatureAlgorithm>
ToSignatureAlgorithmClass(uint16_t algorithm) {
#if defined(OPENSSL_IS_BORINGSSL)
  switch (algorithm) {
    case SSL_SIGN_RSA_PKCS1_SHA256:
      return CustomPrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha256;
    case SSL_SIGN_RSA_PKCS1_SHA384:
      return CustomPrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha384;
    case SSL_SIGN_RSA_PKCS1_SHA512:
      return CustomPrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha512;
    case SSL_SIGN_ECDSA_SECP256R1_SHA256:
      return CustomPrivateKeySigner::SignatureAlgorithm::kEcdsaSecp256r1Sha256;
    case SSL_SIGN_ECDSA_SECP384R1_SHA384:
      return CustomPrivateKeySigner::SignatureAlgorithm::kEcdsaSecp384r1Sha384;
    case SSL_SIGN_ECDSA_SECP521R1_SHA512:
      return CustomPrivateKeySigner::SignatureAlgorithm::kEcdsaSecp521r1Sha512;
    case SSL_SIGN_RSA_PSS_RSAE_SHA256:
      return CustomPrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha256;
    case SSL_SIGN_RSA_PSS_RSAE_SHA384:
      return CustomPrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha384;
    case SSL_SIGN_RSA_PSS_RSAE_SHA512:
      return CustomPrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha512;
  }
#endif  // OPENSSL_IS_BORINGSSL
  return absl::InvalidArgumentError("Unknown signature algorithm.");
}

#if defined(OPENSSL_IS_BORINGSSL)
void TlsOffloadSignDoneCallback(TlsPrivateKeyOffloadContext* ctx,
                                absl::StatusOr<std::string> signed_data) {
  LOG(ERROR) << "anasazalar";
  if (signed_data.ok()) {
    ctx->signed_bytes = std::move(signed_data);

    // Notify the TSI layer to re-enter the handshake.
    // This call is thread-safe as per TSI requirements for the callback.
    if (ctx->notify_cb) {
      LOG(ERROR) << "anasazalar";
      ctx->notify_cb(TSI_OK, ctx->notify_user_data, nullptr, 0, nullptr);
      LOG(ERROR) << "anasazalar";
    }
  } else {
    ctx->signed_bytes = signed_data.status();
    // Notify the TSI layer to re-enter the handshake.
    // This call is thread-safe as per TSI requirements for the callback.
    if (ctx->notify_cb) {
      LOG(ERROR) << "anasazalar";
      ctx->notify_cb(TSI_INTERNAL_ERROR, ctx->notify_user_data, nullptr, 0,
                      nullptr);
    }
  }
}

enum ssl_private_key_result_t TlsPrivateKeySignWrapper(
    SSL* ssl, uint8_t* /*out*/, size_t* /*out_len*/, size_t /*max_out*/,
    uint16_t signature_algorithm, const uint8_t* in, size_t in_len) {
  TlsPrivateKeyOffloadContext* ctx = GetTlsPrivateKeyOffloadContext(ssl);
  // Create the completion callback by binding the current context.
  auto done_callback = absl::bind_front(TlsOffloadSignDoneCallback, ctx);

  // Call the user's async sign function
  // The contract with the user is that they MUST invoke the callback when
  // complete in their implementation, and their impl MUST not block.
  auto algorithm = ToSignatureAlgorithmClass(signature_algorithm);

  if (!algorithm.ok()) {
    return ssl_private_key_failure;
  }

  SSL_CTX* ssl_ctx = SSL_get_SSL_CTX(ssl);
  if (ssl_ctx == nullptr) {
    LOG(ERROR) << "Unexpected error obtaining SSL_CTX object from SSL: ";
    SSL_CTX_free(ssl_ctx);
    return ssl_private_key_failure;
  }

  CustomPrivateKeySigner* signer = GetCustomPrivateKeySigner(ssl_ctx);
  if (signer != nullptr) {
    LOG(ERROR) << "anasazalar";
    signer->Sign(absl::string_view(reinterpret_cast<const char*>(in), in_len),
                 *algorithm, done_callback);
    LOG(ERROR) << "anasazalar";
  }
  LOG(ERROR) << "anasazalar";

  // The operation is not completed. Tell BoringSSL to wait for the signature
  // result.
  return ssl_private_key_retry;
}

enum ssl_private_key_result_t TlsPrivateKeyOffloadComplete(SSL* ssl,
                                                           uint8_t* out,
                                                           size_t* out_len,
                                                           size_t max_out) {
  LOG(ERROR) << "anasazalar";
  TlsPrivateKeyOffloadContext* ctx = GetTlsPrivateKeyOffloadContext(ssl);

  if (!ctx->signed_bytes.ok()) {
    LOG(ERROR) << "anasazalar";
    return ssl_private_key_failure;
  }
  // Important bit is moving the signed data where it needs to go
  const std::string& signed_data = *ctx->signed_bytes;
  if (signed_data.length() > max_out) {
    LOG(ERROR) << "anasazalar";
    // Result is too large.
    return ssl_private_key_failure;
  }
  memcpy(out, signed_data.data(), signed_data.length());
  *out_len = signed_data.length();
  // Tell BoringSSL we're done
  return ssl_private_key_success;
}
#endif  // OPENSSL_IS_BORINGSSL

}  // namespace grpc_core
