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

#include "src/core/credentials/transport/tls/private_key_offload_util.h"

#include <openssl/ssl.h>

#include <cstdint>
#include <string>
#include <utility>

#include "src/core/tsi/transport_security_interface.h"
#include "src/core/util/grpc_check.h"
#include "absl/functional/bind_front.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

static int g_ssl_ex_private_key_offload_ex_index = -1;

namespace grpc_core {

absl::StatusOr<uint16_t> ToOpenSslSignatureAlgorithm(
    SignatureAlgorithm algorithm) {
  switch (algorithm) {
    case SignatureAlgorithm::kRsaPkcs1Sha256:
      return SSL_SIGN_RSA_PKCS1_SHA256;
    case SignatureAlgorithm::kRsaPkcs1Sha384:
      return SSL_SIGN_RSA_PKCS1_SHA384;
    case SignatureAlgorithm::kRsaPkcs1Sha512:
      return SSL_SIGN_RSA_PKCS1_SHA512;
    case SignatureAlgorithm::kEcdsaSecp256r1Sha256:
      return SSL_SIGN_ECDSA_SECP256R1_SHA256;
    case SignatureAlgorithm::kEcdsaSecp384r1Sha384:
      return SSL_SIGN_ECDSA_SECP384R1_SHA384;
    case SignatureAlgorithm::kEcdsaSecp521r1Sha512:
      return SSL_SIGN_ECDSA_SECP521R1_SHA512;
    case SignatureAlgorithm::kRsaPssRsaeSha256:
      return SSL_SIGN_RSA_PSS_RSAE_SHA256;
    case SignatureAlgorithm::kRsaPssRsaeSha384:
      return SSL_SIGN_RSA_PSS_RSAE_SHA384;
    case SignatureAlgorithm::kRsaPssRsaeSha512:
      return SSL_SIGN_RSA_PSS_RSAE_SHA512;
  }
  return absl::InvalidArgumentError("Unknown signature algorithm.");
}

absl::StatusOr<SignatureAlgorithm> ToSignatureAlgorithmClass(
    uint16_t algorithm) {
  switch (algorithm) {
    case SSL_SIGN_RSA_PKCS1_SHA256:
      return SignatureAlgorithm::kRsaPkcs1Sha256;
    case SSL_SIGN_RSA_PKCS1_SHA384:
      return SignatureAlgorithm::kRsaPkcs1Sha384;
    case SSL_SIGN_RSA_PKCS1_SHA512:
      return SignatureAlgorithm::kRsaPkcs1Sha512;
    case SSL_SIGN_ECDSA_SECP256R1_SHA256:
      return SignatureAlgorithm::kEcdsaSecp256r1Sha256;
    case SSL_SIGN_ECDSA_SECP384R1_SHA384:
      return SignatureAlgorithm::kEcdsaSecp384r1Sha384;
    case SSL_SIGN_ECDSA_SECP521R1_SHA512:
      return SignatureAlgorithm::kEcdsaSecp521r1Sha512;
    case SSL_SIGN_RSA_PSS_RSAE_SHA256:
      return SignatureAlgorithm::kRsaPssRsaeSha256;
    case SSL_SIGN_RSA_PSS_RSAE_SHA384:
      return SignatureAlgorithm::kRsaPssRsaeSha384;
    case SSL_SIGN_RSA_PSS_RSAE_SHA512:
      return SignatureAlgorithm::kRsaPssRsaeSha512;
  }
  return absl::InvalidArgumentError("Unknown signature algorithm.");
}

void SetPrivateKeyOffloadIndex(int index) {
  g_ssl_ex_private_key_offload_ex_index = index;
  GRPC_CHECK_NE(g_ssl_ex_private_key_offload_ex_index, -1);
}

int GetPrivateKeyOffloadIndex() {
  return g_ssl_ex_private_key_offload_ex_index;
}

void TlsOffloadSignDoneCallback(TlsPrivateKeyOffloadContext* ctx,
                                absl::StatusOr<std::string> signed_data) {
  if (signed_data.ok()) {
    ctx->signed_bytes = std::move(signed_data);

    // Notify the TSI layer to re-enter the handshake.
    // This call is thread-safe as per TSI requirements for the callback.
    if (ctx->notify_cb) {
      std::string bytes_to_send = *ctx->signed_bytes;
      const unsigned char* bytes_to_send_ptr =
          reinterpret_cast<const unsigned char*>(bytes_to_send.c_str());
      ctx->notify_cb(TSI_OK, ctx->notify_user_data, bytes_to_send_ptr,
                     bytes_to_send.length(), *ctx->handshaker_result);
    }
  } else {
    ctx->signed_bytes = signed_data.status();
    // Notify the TSI layer to re-enter the handshake.
    // This call is thread-safe as per TSI requirements for the callback.
    if (ctx->notify_cb) {
      ctx->notify_cb(TSI_INTERNAL_ERROR, ctx->notify_user_data, nullptr, 0,
                     *ctx->handshaker_result);
    }
  }
}

enum ssl_private_key_result_t TlsPrivateKeySignWrapper(
    SSL* ssl, uint8_t* /*out*/, size_t* /*out_len*/, size_t /*max_out*/,
    uint16_t signature_algorithm, const uint8_t* in, size_t in_len) {
  TlsPrivateKeyOffloadContext* ctx = static_cast<TlsPrivateKeyOffloadContext*>(
      SSL_get_ex_data(ssl, g_ssl_ex_private_key_offload_ex_index));
  // Create the completion callback by binding the current context.
  auto done_callback = absl::bind_front(TlsOffloadSignDoneCallback, ctx);

  // Call the user's async sign function
  // The contract with the user is that they MUST invoke the callback when
  // complete in their implementation, and their impl MUST not block.
  auto algorithm = ToSignatureAlgorithmClass(signature_algorithm);

  if (!algorithm.ok()) {
    return ssl_private_key_failure;
  }

  ctx->private_key_sign(
      absl::string_view(reinterpret_cast<const char*>(in), in_len), *algorithm,
      done_callback);

  // The operation is not completed. Tell BoringSSL to wait for the signature
  // result.
  return ssl_private_key_retry;
}

enum ssl_private_key_result_t TlsPrivateKeyOffloadComplete(SSL* ssl,
                                                           uint8_t* out,
                                                           size_t* out_len,
                                                           size_t max_out) {
  TlsPrivateKeyOffloadContext* ctx = static_cast<TlsPrivateKeyOffloadContext*>(
      SSL_get_ex_data(ssl, g_ssl_ex_private_key_offload_ex_index));

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

}  // namespace grpc_core
