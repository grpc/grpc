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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace experimental {

// Opaque representation of a CRL
class Crl {
 public:
  static absl::StatusOr<std::unique_ptr<Crl>> Parse(
      absl::string_view crl_string);
  virtual ~Crl() = default;

 protected:
  Crl() = default;

 private:
  std::string raw_crl_;
};

// Representation of a cert to be used to fetch its associated CRL.
class Cert {
 public:
  Cert() = default;
  explicit Cert(absl::string_view issuer) : issuer_(issuer) {}
  absl::string_view GetIssuer() { return issuer_; }

 protected:
 private:
  absl::string_view raw_cert_;
  std::string issuer_;
};

// The base class for CRL Provider implementations.
class CrlProvider {
 public:
  CrlProvider() = default;
  // Get the CRL associated with a certificate. Read-only.
  virtual std::shared_ptr<Crl> GetCrl(const Cert& cert) = 0;
  virtual void CrlReadErrorCallback(absl::Status) = 0;
};

}  // namespace experimental
}  // namespace grpc_core

#endif  // GRPC_GRPC_CRL_PROVIDER_H
