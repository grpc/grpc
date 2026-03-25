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

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"

namespace grpc_core {

using PrivateKey = std::variant<std::string, std::shared_ptr<PrivateKeySigner>>;

class CertificateSelector {
 public:
  struct SelectCertInfo {
    std::string sni;
  };

  struct SelectCertResult {
    // The certificates must be DER-encoded here.
    std::vector<std::string> certificate_chain;
    PrivateKey private_key;
  };

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

#endif  // GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_GRPC_TLS_CERTIFICATE_SELECTOR_H
