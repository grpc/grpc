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
 public:
  struct SelectCertificateInfo {
    std::string sni;
  };

  // TODO(lwge): This should be an opaque struct when moved to a public header.
  struct SelectCertificateResult {
#if defined(OPENSSL_IS_BORINGSSL)
    std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> cert_chain;
    std::variant<bssl::UniquePtr<EVP_PKEY>, std::shared_ptr<PrivateKeySigner>>
        private_key;
#endif
  };

  // Returns a SelectCertificateResult with DER-encoded cert chains, and
  // DER-encoded private key string or a signer.
  static absl::StatusOr<SelectCertificateResult> CreateSelectCertificateResult(
      const std::vector<std::string>& cert_chain,
      std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
          private_key);

  // Returns a SelectCertificateResult with PEM-encoded cert chains, and
  // PEM-encoded private key string or a signer.
  static absl::StatusOr<SelectCertificateResult> CreateSelectCertificateResult(
      absl::string_view cert_chain,
      std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
          private_key);

  // To cancel the async `SelectCertificate` call. Users must implement this for
  // correct cancellation behavior.
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
  // return. It should either return the result synchronously or an async handle
  // to support cancellation. In the asynchronous case, the implementation is
  // expected to invoke `OnSelectCertificateComplete` when the cert selection is
  // done. Users should use the appropriate `CreateSelectCertificateResults`
  // function to create the `SelectCertificateResult` struct.
  virtual std::variant<absl::StatusOr<SelectCertificateResult>,
                       std::shared_ptr<AsyncCertificateSelectionHandle>>
  SelectCertificate(const SelectCertificateInfo&,
                    OnSelectCertificateComplete) = 0;

  // Cancels the async select cert call corresponding to the handle.
  virtual void Cancel(std::shared_ptr<AsyncCertificateSelectionHandle>) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_GRPC_TLS_CERTIFICATE_SELECTOR_H
