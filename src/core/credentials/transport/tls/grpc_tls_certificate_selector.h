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

#ifndef GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_GRPC_TLS_CERTIFICATE_SELECTOR_H
#define GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_GRPC_TLS_CERTIFICATE_SELECTOR_H

#include <grpc/private_key_signer.h>
#include <openssl/bio.h>

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Performs server-side certificate selection during the handshake based on the
// SNI. Users must implement the `SelectCertificate` and `Cancel` methods.
// The implementation must be thread-safe, as `SelectCertificate` may be called
// for multiple TLS handshakes at the same time.
class CertificateSelector {
#if defined(OPENSSL_IS_BORINGSSL)
 public:
  struct SelectCertificateInfo {
    std::string sni;
  };

  // TODO(lwge): This should be an opaque struct when moved to a public header.
  struct SelectCertificateResult {
    std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> certificate_chain;
    std::variant<bssl::UniquePtr<EVP_PKEY>, std::shared_ptr<PrivateKeySigner>>
        private_key;
  };

  // Returns a SelectCertificateResult given a DER-encoded certificate chain,
  // and DER-encoded private key string or a signer.
  static absl::StatusOr<SelectCertificateResult> CreateSelectCertificateResult(
      const std::vector<std::string>& der_cert_chain,
      std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
          der_private_key);

  // Returns a SelectCertificateResult given a PEM-encoded certificate chain,
  // and PEM-encoded private key string or a signer.
  static absl::StatusOr<SelectCertificateResult> CreateSelectCertificateResult(
      absl::string_view pem_cert_chain,
      std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
          pem_private_key);

  // Handle to be passed to `Cancel`, to enable user to clean up any resources
  // used during an asynchronous certificate selection operation that is
  // cancelled.
  class AsyncCertificateSelectionHandle {
   public:
    virtual ~AsyncCertificateSelectionHandle() = default;
  };

  using OnSelectCertificateComplete =
      absl::AnyInvocable<void(absl::StatusOr<SelectCertificateResult>)>;

  virtual ~CertificateSelector() = default;

  // Performs the cert selection based on `SelectCertificateInfo`.
  // Since the client is not required to provide the server name in the
  // ClientHello, the implementation should make a decision by itself on what to
  // return.
  // May return either synchronously or asynchronously.
  // For synchronous returns, directly returns either the selected certificate
  // or a failed status, and the callback will never be invoked.
  // For asynchronous implementations, returns a handle for the asynchronous
  // signing operation. The function argument on_complete must be called by
  // the implementer when the async certificate selection operation is complete.
  // on_complete must not be invoked synchronously within SelectCertificate().
  virtual std::variant<absl::StatusOr<SelectCertificateResult>,
                       std::shared_ptr<AsyncCertificateSelectionHandle>>
  SelectCertificate(const SelectCertificateInfo& info,
                    OnSelectCertificateComplete on_complete) = 0;

  // Cancels the async select cert call corresponding to the handle.
  virtual void Cancel(std::shared_ptr<AsyncCertificateSelectionHandle>) = 0;
#endif  // OPENSSL_IS_BORINGSSL
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_GRPC_TLS_CERTIFICATE_SELECTOR_H
