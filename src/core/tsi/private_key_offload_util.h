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

#ifndef GRPC_SRC_CORE_TSI_PRIVATE_KEY_OFFLOAD_UTIL_H
#define GRPC_SRC_CORE_TSI_PRIVATE_KEY_OFFLOAD_UTIL_H

#include <grpc/private_key_signer.h>
#include <openssl/ssl.h>

#include <memory>
#include <string>

#include "src/core/tsi/transport_security_interface.h"
#include "absl/status/statusor.h"

namespace grpc_core {

class AsyncSigningHandle;

// State associated with an SSL object for async private key operations.
struct TlsPrivateKeyOffloadContext {
  enum SignatureStatus {
    // The signature operation has not yet started.
    kNotStarted,
    // The signature operation has been initiated.
    kStarted,
    // The signature operation is currently in progress waiting for an
    // asynchronous operation.
    kInProgressAsync,
    // The signature operation has completed, and the signed data is available
    // on the cached context.
    kSignatureCompleted,
    // The entire private key offload process for this signature is finished.
    kFinished,
  };

  SignatureStatus status = kNotStarted;
  // The signed_bytes are populated when the signature process is completed if
  // the Private Key offload was successful. If there was an error during the
  // signature, the status will be returned.
  absl::StatusOr<std::string> signed_bytes;
  // The handle for an in-flight async signing operation.
  std::shared_ptr<AsyncSigningHandle> signing_handle;

  // TSI handshake state needed to resume.
  tsi_handshaker* handshaker;
  tsi_handshaker_on_next_done_cb notify_cb;
  tsi_handshaker_result* handshaker_result;
  void* notify_user_data;

  size_t received_bytes_size;
  unsigned char* received_bytes;
  std::string* error;
};

// Returns the TlsPrivateKeyOffloadContext associated with the SSL object.
TlsPrivateKeyOffloadContext* GetTlsPrivateKeyOffloadContext(SSL* ssl);

// Returns the PrivateKeySigner associated with the SSL_CTX object.
PrivateKeySigner* GetPrivateKeySigner(SSL* ssl);

#if defined(OPENSSL_IS_BORINGSSL)
// // Callback function to be invoked when the user's async sign operation is
// // complete.
// void TlsOffloadSignDoneCallback(TlsPrivateKeyOffloadContext* ctx,
//                                 absl::StatusOr<std::string> signed_data);

// enum ssl_private_key_result_t TlsPrivateKeySignWrapper(
//     SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out,
//     uint16_t signature_algorithm, const uint8_t* in, size_t in_len);

enum ssl_private_key_result_t TlsPrivateKeyOffloadComplete(SSL* ssl,
                                                           uint8_t* out,
                                                           size_t* out_len,
                                                           size_t max_out);
#endif  // OPENSSL_IS_BORINGSSL

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TSI_PRIVATE_KEY_OFFLOAD_UTIL_H