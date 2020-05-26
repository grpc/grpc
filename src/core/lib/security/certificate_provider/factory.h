#ifndef GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_FACTORY_H
#define GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_FACTORY_H

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/security/certificate_provider/provider.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

namespace grpc_core {

// Factories for plugins. Each plugin implementation should create its own
// factory implementation and register an instance with the registry.
class CertificateProviderFactory {
 public:
  virtual ~CertificateProviderFactory() {}
  // Name of the plugin.
  virtual const char* name() const = 0;
  // Create a config object with the config type specified by the
  // implementation.
  virtual RefCountedPtr<CertificateProviderConfig> CreateProviderConfig(
      const Json& config_json, grpc_error** error) = 0;
  // Create a CertificateProvider instance from config.
  virtual OrphanablePtr<CertificateProvider> CreateProvider(
      RefCountedPtr<CertificateProviderConfig> config,
      RefCountedPtr<grpc_tls_certificate_distributor> distributor) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_FACTORY_H
