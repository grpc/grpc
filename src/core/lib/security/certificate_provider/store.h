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

#ifndef GRPC_CORE_LIB_SECURITY_SECURITY_PROVIDER_STORE_H
#define GRPC_CORE_LIB_SECURITY_SECURITY_PROVIDER_STORE_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/security/certificate_provider/config.h"
#include "src/core/lib/security/certificate_provider/provider.h"

namespace grpc_core {

class CertificateProviderStore;

// CertificateProviderWrapper allows multiple subchannels to hold reference to
// the same CertificateProvider instance when the provider's configs are the
// same. When all references to the wrapper are gone, the wrapper removes itself
// from the store it belongs to.
class CertificateProviderWrapper
    : public RefCounted<CertificateProviderWrapper> {
 public:
  CertificateProviderWrapper(
      OrphanablePtr<CertificateProvider> child,
      RefCountedPtr<grpc_tls_certificate_distributor> distributor,
      CertificateProviderStore* store);
  ~CertificateProviderWrapper();
  CertificateProvider* child() const;
  grpc_tls_certificate_distributor* distributor() const;

 private:
  OrphanablePtr<CertificateProvider> child_;
  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  CertificateProviderStore* store_;
};

// Global map for the CertificateProvider instances.
class CertificateProviderStore {
 public:
  // If a provider corresponding to the config is found, the wrapper in the map
  // is returned. If no provider is found for a key, a new wrapper is created
  // and returned to the caller.
  RefCountedPtr<CertificateProviderWrapper> CreateOrGetProvider(
      RefCountedPtr<CertificateProviderConfig> key);
  // Remove a provider corresponding to the config from the map. This will be
  // called by the last wrapper holding reference to a provider.
  void RemoveProvider(RefCountedPtr<CertificateProviderConfig> key);

 private:
  // Underlying map for the providers.
  std::unordered_map<
      RefCountedPtr<CertificateProviderConfig>, CertificateProviderWrapper*,
      CertificateProviderConfigHasher, CertificateProviderConfigPred>
      map_;
  // Protects map_.
  Mutex mu_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_SECURITY_PROVIDER_STORE_H
