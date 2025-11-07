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

#ifndef GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_PRIVATE_KEY_OFFLOAD_UTIL_H
#define GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_PRIVATE_KEY_OFFLOAD_UTIL_H

#include <openssl/base.h>
#include <openssl/ssl.h>
#include <openssl/stack.h>
#include <openssl/x509.h>

#include <string>
#include <utility>

#include "src/core/tsi/transport_security_interface.h"
#include "absl/functional/bind_front.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

static int g_ssl_ex_private_key_offload_ex_index = -1;

namespace grpc_core {
// Enum class representing TLS signature algorithm identifiers from BoringSSL.
// The values correspond to the SSL_SIGN_* macros in <openssl/ssl.h>.
enum class SignatureAlgorithm : uint16_t {
  kRsaPkcs1Sha256 = 0x0401,        // SSL_SIGN_RSA_PKCS1_SHA256
  kRsaPkcs1Sha384 = 0x0501,        // SSL_SIGN_RSA_PKCS1_SHA384
  kRsaPkcs1Sha512 = 0x0601,        // SSL_SIGN_RSA_PKCS1_SHA512
  kEcdsaSecp256r1Sha256 = 0x0403,  // SSL_SIGN_ECDSA_SECP256R1_SHA256
  kEcdsaSecp384r1Sha384 = 0x0503,  // SSL_SIGN_ECDSA_SECP384R1_SHA384
  kEcdsaSecp521r1Sha512 = 0x0603,  // SSL_SIGN_ECDSA_SECP521R1_SHA512
  kRsaPssRsaeSha256 = 0x0804,      // SSL_SIGN_RSA_PSS_RSAE_SHA256
  kRsaPssRsaeSha384 = 0x0805,      // SSL_SIGN_RSA_PSS_RSAE_SHA384
  kRsaPssRsaeSha512 = 0x0806,      // SSL_SIGN_RSA_PSS_RSAE_SHA512
};

static void SetPrivateKeyOffloadIndex(int index) {
  g_ssl_ex_private_key_offload_ex_index = index;
  GRPC_CHECK_NE(g_ssl_ex_private_key_offload_ex_index, -1);
}

static int GetPrivateKeyOffloadIndex() {
  return g_ssl_ex_private_key_offload_ex_index;
}

// A user's implementation MUST invoke `done_callback` with the signed bytes.
// This will let gRPC take control when the async operation is complete. MUST
// not block MUST support concurrent calls
using CustomPrivateKeySign = absl::AnyInvocable<void(
    absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm,
    absl::AnyInvocable<void(absl::StatusOr<std::string> signed_data)>
        done_callback)>;

// State associated with an SSL object for async private key operations.
struct TlsPrivateKeyOffloadContext {
  CustomPrivateKeySign private_key_sign;
  absl::StatusOr<std::string> signed_bytes;

  // TSI handshake state needed to resume.
  tsi_handshaker* handshaker;
  tsi_handshaker_on_next_done_cb notify_cb;
  tsi_handshaker_result** handshaker_result;

  void* notify_user_data;
};

// Callback function to be invoked when the user's async sign operation is
// complete.

static void TlsOffloadSignDoneCallback(
    TlsPrivateKeyOffloadContext* ctx, absl::StatusOr<std::string> signed_data) {
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

static enum ssl_private_key_result_t TlsPrivateKeySignWrapper(
    SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out,
    uint16_t signature_algorithm, const uint8_t* in, size_t in_len) {
  TlsPrivateKeyOffloadContext* ctx = static_cast<TlsPrivateKeyOffloadContext*>(
      SSL_get_ex_data(ssl, g_ssl_ex_private_key_offload_ex_index));
  // Create the completion callback by binding the current context.
  auto done_callback = absl::bind_front(TlsOffloadSignDoneCallback, ctx);

  // Call the user's async sign function
  // The contract with the user is that they MUST invoke the callback when
  // complete in their implementation, and their impl MUST not block.
  ctx->private_key_sign(
      absl::string_view(reinterpret_cast<const char*>(in), in_len),
      static_cast<SignatureAlgorithm>(signature_algorithm),
      std::move(done_callback));

  return ssl_private_key_retry;
}

static enum ssl_private_key_result_t TlsPrivateKeyOffloadComplete(
    SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out) {
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

static const SSL_PRIVATE_KEY_METHOD TlsOffloadPrivateKeyMethod = {
    TlsPrivateKeySignWrapper,
    nullptr,  // decrypt not implemented for this use case
    TlsPrivateKeyOffloadComplete};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_PRIVATE_KEY_OFFLOAD_UTIL_H