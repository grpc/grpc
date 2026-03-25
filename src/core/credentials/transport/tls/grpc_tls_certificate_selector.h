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

#ifndef GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_GRPC_TLS_CERTIFICATE_SELECTOR_H_
#define GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_GRPC_TLS_CERTIFICATE_SELECTOR_H_

#include <grpc/private_key_signer.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Performs server-side certificate selection per TLS handshake.
// Users should implement the `SelectCert` API. It should either return the
// result synchronously or an async handle to support cancellation. In the
// asynchronous case, the implementation is expected to invoke
// `OnSelectCertComplete` when the cert selection is done. Users should use the
// appropriate `CreateSelectCertResults` function to create the
// `SelectCertResult` struct.
class CertificateSelector {
 public:
  struct SelectCertInfo {
    std::string sni;
  };

  // TODO(lwge): This should be an opaque struct when moved to a public header.
  struct SelectCertResult {
#if defined(OPENSSL_IS_BORINGSSL)
    std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> cert_chain;
    std::variant<bssl::UniquePtr<EVP_PKEY>, std::shared_ptr<PrivateKeySigner>>
        private_key;
#endif
  };

  // Takes DER-formatted cert chains, and a signer.
  // TODO(lwge): Consider supporting DER private key string.
  static absl::StatusOr<SelectCertResult> CreateSelectCertResult(
      const std::vector<std::string>& cert_chain,
      std::shared_ptr<PrivateKeySigner> private_key_signer);

  // Takes PEM-formatted cert chains, and PEM-formatted private key string or a
  // signer.
  static absl::StatusOr<SelectCertResult> CreateSelectCertResult(
      absl::string_view cert_chain,
      std::variant<absl::string_view, std::shared_ptr<PrivateKeySigner>>
          private_key);

  class AsyncCertSelectionHandle {
   public:
    virtual ~AsyncCertSelectionHandle() = default;
  };

  using OnSelectCertComplete =
      absl::AnyInvocable<void(absl::StatusOr<SelectCertResult>)>;

  virtual ~CertificateSelector() = default;

  virtual std::variant<absl::StatusOr<SelectCertResult>,
                       std::shared_ptr<AsyncCertSelectionHandle>>
  SelectCert(const SelectCertInfo&, OnSelectCertComplete) = 0;

  virtual void Cancel(std::shared_ptr<AsyncCertSelectionHandle>) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_GRPC_TLS_CERTIFICATE_SELECTOR_H_
