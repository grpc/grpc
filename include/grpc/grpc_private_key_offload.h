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

#ifndef GRPC_GRPC_PRIVATE_KEY_OFFLOAD_H
#define GRPC_GRPC_PRIVATE_KEY_OFFLOAD_H

#include <grpc/credentials.h>

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// A user's implementation MUST invoke `OnSignComplete` with the signed bytes.
// This will let gRPC take control when the async operation is complete. MUST
// not block MUST support concurrent calls
class CustomPrivateKeySigner {
 public:
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

  using OnSignComplete = absl::AnyInvocable<void(absl::StatusOr<std::string>)>;

  virtual ~CustomPrivateKeySigner() = default;

  virtual void Sign(absl::string_view data_to_sign,
                    SignatureAlgorithm signature_algorithm,
                    OnSignComplete on_sign_complete) = 0;
};
}  // namespace grpc_core

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Sets the identity ceritifcate provider in the options.
 * The |options| will implicitly take a new ref to the |provider|.
 */
GRPCAPI void grpc_tls_credentials_options_set_identity_certificate_provider(
    grpc_tls_credentials_options* options,
    grpc_tls_certificate_provider* provider);

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Sets the root ceritifcate provider in the options.
 * The |options| will implicitly take a new ref to the |provider|.
 */
GRPCAPI void grpc_tls_credentials_options_set_root_certificate_provider(
    grpc_tls_credentials_options* options,
    grpc_tls_certificate_provider* provider);

#endif  // GRPC_GRPC_PRIVATE_KEY_OFFLOAD_H