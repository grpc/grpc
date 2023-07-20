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

#ifndef GRPC_GRPC_CRL_PROVIDER_H
#define GRPC_GRPC_CRL_PROVIDER_H

#include <memory>
#include <string>

#include "absl/strings/string_view.h"

namespace grpc_core {
namespace experimental {

// Representation of a CRL
class Crl {
 public:
  explicit Crl(const char* crl) : crl_(crl){};

 private:
  absl::string_view crl_;
  // best way to accomplish this? Where to have OpenSSL import?
  // X509_CRL openssl_crl_;
};

// Representation of a Certificate
class Cert {
 public:
  explicit Cert(const char* cert) : cert_(cert){};

 private:
  absl::string_view cert_;
  // best way to accomplish this? Where to have OpenSSL import?
  // X509 openssl_cert_;
};

// The base class for CRL Provider implementations.
class CrlProvider {
 public:
  CrlProvider() {}
  // Get the CRL associated with a certificate. Read-only.
  virtual const Crl* Crl(const Cert& cert) = 0;
  virtual void CrlReadErrorCallback(/*TODO*/) = 0;
};

}  // namespace experimental
}  // namespace grpc_core

#endif  // GRPC_GRPC_CRL_PROVIDER_H
