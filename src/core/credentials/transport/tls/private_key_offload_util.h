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

#include <memory>
#include <string>

#include "src/core/tsi/transport_security_interface.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
// Enum class representing TLS signature algorithm identifiers from BoringSSL.
// The values correspond to the SSL_SIGN_* macros in <openssl/ssl.h>.
enum class SignatureAlgorithm {
  kRsaPkcs1Sha256,
  kRsaPkcs1Sha384,
  kRsaPkcs1Sha512,
  kEcdsaSecp256r1Sha256,
  kEcdsaSecp384r1Sha384,
  kEcdsaSecp521r1Sha512,
  kRsaPssRsaeSha256,
  kRsaPssRsaeSha384,
  kRsaPssRsaeSha512,
};

absl::StatusOr<uint16_t> ToOpenSslSignatureAlgorithm(
    SignatureAlgorithm algorithm);

void SetPrivateKeyOffloadIndex(int index);

int GetPrivateKeyOffloadIndex();

// A user's implementation MUST invoke `done_callback` with the signed bytes.
// This will let gRPC take control when the async operation is complete. MUST
// not block MUST support concurrent calls
using CustomPrivateKeySign = std::function<void(
    absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm,
    std::function<void(absl::StatusOr<std::string> signed_data)>
        done_callback)>;

// State associated with an SSL object for async private key operations.
struct TlsPrivateKeyOffloadContext {
  explicit TlsPrivateKeyOffloadContext(CustomPrivateKeySign private_key_sign)
      : private_key_sign(private_key_sign) {}

  const CustomPrivateKeySign private_key_sign;
  absl::StatusOr<std::string> signed_bytes;

  // TSI handshake state needed to resume.
  tsi_handshaker* handshaker;
  tsi_handshaker_on_next_done_cb notify_cb;
  tsi_handshaker_result** handshaker_result;

  void* notify_user_data;
};

// Callback function to be invoked when the user's async sign operation is
// complete.
void TlsOffloadSignDoneCallback(TlsPrivateKeyOffloadContext* ctx,
                                absl::StatusOr<std::string> signed_data);

enum ssl_private_key_result_t TlsPrivateKeySignWrapper(
    SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out,
    uint16_t signature_algorithm, const uint8_t* in, size_t in_len);

enum ssl_private_key_result_t TlsPrivateKeyOffloadComplete(SSL* ssl,
                                                           uint8_t* out,
                                                           size_t* out_len,
                                                           size_t max_out);

const SSL_PRIVATE_KEY_METHOD TlsOffloadPrivateKeyMethod = {
    TlsPrivateKeySignWrapper,
    nullptr,  // decrypt not implemented for this use case
    TlsPrivateKeyOffloadComplete};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_PRIVATE_KEY_OFFLOAD_UTIL_H