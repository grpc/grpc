#ifndef GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_REGISTRY_H
#define GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_REGISTRY_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <unordered_map>

namespace grpc_core {

class CertificateProviderFactory;

// Global registry for all the certificate provider plugins.
class CertificateProviderRegistry {
 public:
  // Global initialization of the registry.
  static void InitRegistry();
  // Global shutdown of the registry.
  static void ShutdownRegistry();
  // Register a provider with the registry. Can only be called after calling
  // InitRegistry(). The key of the factory is extracted from factory parameter
  // with method CertificateProviderFactory::name. If the same key is registered
  // twice, an exception is raised.
  static void RegisterProvider(
      std::unique_ptr<CertificateProviderFactory> factory);

  // Returns the factory for the plugin keyed by name.
  static CertificateProviderFactory* GetFactory(const std::string& name);

 private:
  std::unordered_map<std::string /* plugin name */,
                     std::unique_ptr<CertificateProviderFactory>>
      registry_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_REGISTRY_H
