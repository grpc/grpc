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

#ifndef GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_CERTIFICATE_PROVIDER_FACTORY_H
#define GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_CERTIFICATE_PROVIDER_FACTORY_H

#include <grpc/support/port_platform.h>

#include <string>

#include <grpc/grpc_security.h>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

// Factories for plugins. Each plugin implementation should create its own
// factory implementation and register an instance with the registry.
class CertificateProviderFactory {
 public:
  // Interface for configs for CertificateProviders.
  class Config : public RefCounted<Config> {
   public:
    ~Config() override = default;

    // Name of the type of the CertificateProvider. Unique to each type of
    // config.
    virtual const char* name() const = 0;

    virtual std::string ToString() const = 0;
  };

  virtual ~CertificateProviderFactory() = default;

  // Name of the plugin.
  virtual const char* name() const = 0;

  virtual RefCountedPtr<Config> CreateCertificateProviderConfig(
      const Json& config_json, grpc_error_handle* error) = 0;

  // Create a CertificateProvider instance from config.
  virtual RefCountedPtr<grpc_tls_certificate_provider>
  CreateCertificateProvider(RefCountedPtr<Config> config) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_CERTIFICATE_PROVIDER_FACTORY_H
