#include <grpc/support/port_platform.h>

#include "src/core/lib/security/certificate_provider/factory.h"
#include "src/core/lib/security/certificate_provider/registry.h"

namespace grpc_core {

static CertificateProviderRegistry* g_registry;

void CertificateProviderRegistry::InitRegistry() {
  if (g_registry == nullptr) g_registry = new CertificateProviderRegistry();
}

void CertificateProviderRegistry::ShutdownRegistry() {
  delete g_registry;
  g_registry = nullptr;
}

void CertificateProviderRegistry::RegisterProvider(
    std::unique_ptr<CertificateProviderFactory> factory) {
  auto result =
      g_registry->registry_.insert({factory->name(), std::move(factory)});
  // If the result's second element is false, it means there exists a factory of
  // the name name in the registry already.
  GPR_ASSERT(result.second);
}

CertificateProviderFactory* CertificateProviderRegistry::GetFactory(
    const std::string& name) {
  auto it = g_registry->registry_.find(name);
  if (it == g_registry->registry_.end()) {
    return nullptr;
  } else {
    return it->second.get();
  }
}

}  // namespace grpc_core
