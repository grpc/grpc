//
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
//

#ifndef GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_CERTIFICATE_PROVIDER_REGISTRY_H
#define GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_CERTIFICATE_PROVIDER_REGISTRY_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <vector>

#include "absl/strings/string_view.h"

#include "src/core/lib/security/certificate_provider/certificate_provider_factory.h"

namespace grpc_core {

// Global registry for all the certificate provider plugins.
class CertificateProviderRegistry {
 public:
  class Builder {
   public:
    // Register a provider with the registry. Can only be called after calling
    // InitRegistry(). The key of the factory is extracted from factory
    // parameter with method CertificateProviderFactory::name. If the same key
    // is registered twice, an exception is raised.
    void RegisterCertificateProviderFactory(
        std::unique_ptr<CertificateProviderFactory> factory);

    CertificateProviderRegistry Build();

   private:
    std::vector<std::unique_ptr<CertificateProviderFactory>> factories_;
  };

  CertificateProviderRegistry(const CertificateProviderRegistry&) = delete;
  CertificateProviderRegistry& operator=(const CertificateProviderRegistry&) =
      delete;
  CertificateProviderRegistry(CertificateProviderRegistry&&) = default;
  CertificateProviderRegistry& operator=(CertificateProviderRegistry&&) =
      default;

  // Returns the factory for the plugin keyed by name.
  CertificateProviderFactory* LookupCertificateProviderFactory(
      absl::string_view name) const;

 private:
  CertificateProviderRegistry() = default;

  std::vector<std::unique_ptr<CertificateProviderFactory>> factories_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_CERTIFICATE_PROVIDER_REGISTRY_H
