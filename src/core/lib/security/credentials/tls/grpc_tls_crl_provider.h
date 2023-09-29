//
//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CRL_PROVIDER_H
#define GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CRL_PROVIDER_H

#include <grpc/support/port_platform.h>
#include <openssl/crypto.h>
#include <grpc/grpc_crl_provider.h>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/status/statusor.h"

namespace grpc_core {
namespace experimental {

class CrlImpl : public Crl {
 public:
  // Takes ownership of the X509_CRL pointer.
  CrlImpl(X509_CRL* crl, const std::string& issuer);
  // Makes a copy of the X509_CRL
  CrlImpl(const CrlImpl& other);
  ~CrlImpl() override;
  // Returns a string view representation of the issuer pulled from the CRL.
  absl::string_view Issuer() override;
  // The caller should not take ownership of the returned pointer.
  X509_CRL* crl() const;
  static absl::StatusOr<CrlImpl> Create(X509_CRL* crl);

 private:
  X509_CRL* crl_;
  const std::string issuer_;
};

class CertificateInfoImpl : public CertificateInfo {
 public:
  explicit CertificateInfoImpl(absl::string_view issuer);
  // Returns a string representation of the issuer pulled from the
  // certificate.
  absl::string_view Issuer() const override;

 private:
  const std::string issuer_;
};

}  // namespace experimental
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CRL_PROVIDER_H