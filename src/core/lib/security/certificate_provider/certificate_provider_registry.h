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

#ifndef GRPC_SRC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_CERTIFICATE_PROVIDER_REGISTRY_H
#define GRPC_SRC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_CERTIFICATE_PROVIDER_REGISTRY_H

#include <grpc/support/port_platform.h>

#include <map>
#include <memory>
#include <utility>

#include "absl/strings/string_view.h"

#include "src/core/lib/security/certificate_provider/certificate_provider_factory.h"

namespace grpc_core {

// Global registry for all the certificate provider plugins.
class CertificateProviderRegistry {
 private:
  using FactoryMap =
      std::map<absl::string_view, std::unique_ptr<CertificateProviderFactory>>;

 public:
  class Builder {
   public:
    // Register a provider with the registry. The key of the factory is
    // extracted from factory parameter with method
    // CertificateProviderFactory::name. The registry with a given name
    // cannot be registered twice.
    void RegisterCertificateProviderFactory(
        std::unique_ptr<CertificateProviderFactory> factory);

    CertificateProviderRegistry Build();

   private:
    FactoryMap factories_;
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
  explicit CertificateProviderRegistry(FactoryMap factories)
      : factories_(std::move(factories)) {}

  FactoryMap factories_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_CERTIFICATE_PROVIDER_REGISTRY_H
