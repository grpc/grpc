//
// Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_PROVIDER_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_PROVIDER_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc_security.h>
#include <string.h>

#include "absl/container/inlined_vector.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"

struct grpc_tls_certificate_provider
    : public grpc_core::RefCounted<grpc_tls_certificate_provider> {
 public:
  grpc_tls_certificate_provider() = default;

  virtual ~grpc_tls_certificate_provider() = default;

  virtual grpc_core::RefCountedPtr<grpc_tls_certificate_distributor>
  distributor() const = 0;
};

namespace grpc_core {

// A basic provider class that will get credentials from string during
// initialization.
class StaticDataCertificateProvider : public grpc_tls_certificate_provider {
 public:
  StaticDataCertificateProvider(std::string root_certificate,
                                std::string private_key,
                                std::string identity_certificate);

  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const override {
    return distributor_;
  }

 private:
  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  std::string root_certificate_;
  std::string private_key_;
  std::string identity_certificate_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_PROVIDER_H
