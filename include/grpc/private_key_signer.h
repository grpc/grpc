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

#ifndef GRPC_PRIVATE_KEY_SIGNER_H
#define GRPC_PRIVATE_KEY_SIGNER_H

#include <grpc/credentials.h>

#include <memory>
#include <string>
#include <variant>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class PrivateKeySigner {
 public:
  // A handle for an asynchronous signing operation.
  //
  // When `PrivateKeySigner::Sign` is implemented asynchronously, it returns an
  // instance of a concrete implementation of this class. This handle is used
  // to manage the asynchronous signing operation and can be used to cancel the
  // operation via `PrivateKeySigner::Cancel`.
  //
  // Users must provide their own concrete implementation of this class. The
  // handle can store any state needed for the asynchronous operation.
  class AsyncSigningHandle {
   public:
    virtual ~AsyncSigningHandle() = default;
  };

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

  virtual ~PrivateKeySigner() = default;

  // Signs data_to_sign.
  // May return either synchronously or asynchronously.
  // For synchronous returns, directly returns either the signed bytes
  // or a failed status, and the callback will never be invoked.
  // For asynchronous implementations, returns a handle for the asynchronous
  // signing operation. The function argument on_sign_complete must be called by
  // the implementer when the async signing operation is complete.
  virtual std::variant<absl::StatusOr<std::string>,
                       std::shared_ptr<AsyncSigningHandle>>
  Sign(absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm,
       OnSignComplete on_sign_complete) = 0;

  // Cancels an in-flight async signing operation using a handle returned
  // from a previous call to Sign().
  virtual void Cancel(std::shared_ptr<AsyncSigningHandle> handle) = 0;
};
}  // namespace grpc_core

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Adds a identity private key and a identity certificate chain to
 * grpc_tls_identity_pairs. This function will make an internal copy of
 * |cert_chain| and take ownership of |private_key|.
 */
GRPCAPI void grpc_tls_identity_pairs_add_pair_with_signer(
    grpc_tls_identity_pairs* pairs,
    std::shared_ptr<grpc_core::PrivateKeySigner> private_key_signer,
    const char* cert_chain);

#endif /* GRPC_PRIVATE_KEY_SIGNER_H */