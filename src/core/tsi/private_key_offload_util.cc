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
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include "src/core/tsi/transport_security_interface.h"
#include "absl/functional/bind_front.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

absl::StatusOr<PrivateKeySigner::SignatureAlgorithm> ToSignatureAlgorithmClass(
    uint16_t algorithm) {
#if defined(OPENSSL_IS_BORINGSSL)
  switch (algorithm) {
    case SSL_SIGN_RSA_PKCS1_SHA256:
      return PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha256;
    case SSL_SIGN_RSA_PKCS1_SHA384:
      return PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha384;
    case SSL_SIGN_RSA_PKCS1_SHA512:
      return PrivateKeySigner::SignatureAlgorithm::kRsaPkcs1Sha512;
    case SSL_SIGN_ECDSA_SECP256R1_SHA256:
      return PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp256r1Sha256;
    case SSL_SIGN_ECDSA_SECP384R1_SHA384:
      return PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp384r1Sha384;
    case SSL_SIGN_ECDSA_SECP521R1_SHA512:
      return PrivateKeySigner::SignatureAlgorithm::kEcdsaSecp521r1Sha512;
    case SSL_SIGN_RSA_PSS_RSAE_SHA256:
      return PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha256;
    case SSL_SIGN_RSA_PSS_RSAE_SHA384:
      return PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha384;
    case SSL_SIGN_RSA_PSS_RSAE_SHA512:
      return PrivateKeySigner::SignatureAlgorithm::kRsaPssRsaeSha512;
  }
#endif  // OPENSSL_IS_BORINGSSL
  return absl::InvalidArgumentError("Unknown signature algorithm.");
}

#if defined(OPENSSL_IS_BORINGSSL)
void TlsOffloadSignDoneCallback(TlsPrivateKeyOffloadContext* ctx,
                                absl::StatusOr<std::string> signed_data) {
  std::cout << "In TlsOffloadSignDoneCallback\n";
  ctx->signed_bytes = std::move(signed_data);
  if (ctx->status != TlsPrivateKeyOffloadContext::kInProgressAsync) {
    std::cout << "greg1\n";
    ctx->status = TlsPrivateKeyOffloadContext::kSignatureCompleted;
    return;
  }
  ctx->status = TlsPrivateKeyOffloadContext::kSignatureCompleted;
  if (!ctx->signed_bytes.ok()) {
    std::cout << "greg2\n";
    // Notify the TSI layer to re-enter the handshake.
    // This call is thread-safe as per TSI requirements for the callback.
    if (ctx->notify_cb) {
      std::cout << "call notify_cb\n";
      ctx->notify_cb(TSI_INTERNAL_ERROR, ctx->notify_user_data, nullptr, 0,
                     ctx->handshaker_result);
    }
    return;
  }
  std::cout << "greg3\n";
  const uint8_t* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  // Once the signed bytes are obtained, wrap an empty callback to
  // tsi_handshaker_next to resume the pending async operation.
  tsi_result result = tsi_handshaker_next(
      ctx->handshaker, nullptr, 0, &bytes_to_send, &bytes_to_send_size,
      &ctx->handshaker_result, ctx->notify_cb, ctx->notify_user_data);
  std::cout << "greg4\n";
  if (result != TSI_ASYNC) {
    // Notify the TSI layer to re-enter the handshake. This call is
    // thread-safe as per TSI requirements for the callback.
    std::cout << "greg5\n";
    if (ctx->notify_cb) {
      ctx->notify_cb(result, ctx->notify_user_data, bytes_to_send,
                     bytes_to_send_size, ctx->handshaker_result);
    }
  }
}

enum ssl_private_key_result_t TlsPrivateKeySignWrapper(
    SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out,
    uint16_t signature_algorithm, const uint8_t* in, size_t in_len) {
  TlsPrivateKeyOffloadContext* ctx = GetTlsPrivateKeyOffloadContext(ssl);
  ctx->status = TlsPrivateKeyOffloadContext::kStarted;
  // Create the completion callback by binding the current context.
  auto done_callback = absl::bind_front(TlsOffloadSignDoneCallback, ctx);
  // Call the user's async sign function
  // The contract with the user is that they MUST invoke the callback when
  // complete in their implementation, and their impl MUST not block.
  auto algorithm = ToSignatureAlgorithmClass(signature_algorithm);
  if (!algorithm.ok()) {
    return ssl_private_key_failure;
  }
  PrivateKeySigner* signer = GetPrivateKeySigner(ssl);
  if (signer == nullptr) {
    return ssl_private_key_failure;
  }
  auto result =
      signer->Sign(absl::string_view(reinterpret_cast<const char*>(in), in_len),
                   *algorithm, std::move(done_callback));
  // Handle synchronous return.
  if (auto* status_or_string =
          std::get_if<absl::StatusOr<std::string>>(&result)) {
    if (status_or_string->ok()) {
      std::string out_bytes = **status_or_string;
      if (out_bytes.size() > max_out) {
        return ssl_private_key_failure;
      }
      *out_len = out_bytes.size();
      memcpy(out, out_bytes.c_str(), *out_len);
      return ssl_private_key_success;
    } else {
      return ssl_private_key_failure;
    }
  }
  // Handle asynchronous return.
  if (auto* handle =
          std::get_if<std::shared_ptr<AsyncSigningHandle>>(&result)) {
    std::cout << "Greg10\n";
    ctx->signing_handle = std::move(*handle);
    ctx->status = TlsPrivateKeyOffloadContext::kInProgressAsync;
    return ssl_private_key_retry;
  }
  // Should never be reached.
  std::cout << "Greg11\n";
  return ssl_private_key_failure;
}

enum ssl_private_key_result_t TlsPrivateKeyOffloadComplete(SSL* ssl,
                                                           uint8_t* out,
                                                           size_t* out_len,
                                                           size_t max_out) {
  TlsPrivateKeyOffloadContext* ctx = GetTlsPrivateKeyOffloadContext(ssl);
  if (!ctx->signed_bytes.ok()) {
    return ssl_private_key_failure;
  }
  // Important bit is moving the signed data where it needs to go
  const std::string& signed_data = *ctx->signed_bytes;
  if (signed_data.length() > max_out) {
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
