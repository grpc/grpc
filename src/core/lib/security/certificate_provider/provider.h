#ifndef GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_PROVIDER_H
#define GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_PROVIDER_H

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/security/certificate_provider/config.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

#include <sstream>

#include "absl/strings/str_cat.h"

namespace grpc_core {

class CertificateProviderConfig;

// Interface for a plugin that handles the process to fetch credentials and
// validation contexts. Implementations are free to rely on local or remote
// sources to fetch the latest secrets, and free to share any state among
// different instances as they deem fit.
//
// When the credentials and validation contexts become valid or changed, a
// CertificateProvider should notify its distributor.
class CertificateProvider : public InternallyRefCounted<CertificateProvider> {
 public:
  // Providers are passed a distributor to receive the credentials and
  // validation contexts updates. A provider must notify the distributor with
  // appropriate interface when they become available or changed.
  explicit CertificateProvider(
      RefCountedPtr<CertificateProviderConfig> config,
      RefCountedPtr<grpc_tls_certificate_distributor> distributor)
      : distributor_(std::move(distributor)),
        interested_parties_(grpc_pollset_set_create()),
        config_(config) {}

  virtual ~CertificateProvider() {
    grpc_pollset_set_destroy(interested_parties_);
  }

  grpc_pollset_set* interested_parties() const { return interested_parties_; }
  RefCountedPtr<CertificateProviderConfig> config() { return config_; }

 protected:
  grpc_tls_certificate_distributor* distributor() const {
    return distributor_.get();
  }

 private:
  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  grpc_pollset_set* interested_parties_;
  RefCountedPtr<CertificateProviderConfig> config_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_PROVIDER_H
