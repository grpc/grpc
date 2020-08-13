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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/certificate_provider/factory.h"
#include "src/core/lib/security/certificate_provider/provider.h"
#include "src/core/lib/security/certificate_provider/registry.h"
#include "src/core/lib/security/certificate_provider/store.h"

namespace grpc_core {

CertificateProviderWrapper::CertificateProviderWrapper(
    OrphanablePtr<CertificateProvider> child,
    RefCountedPtr<grpc_tls_certificate_distributor> distributor,
    CertificateProviderStore* store)
    : child_(std::move(child)),
      distributor_(std::move(distributor)),
      store_(store) {}

CertificateProviderWrapper::~CertificateProviderWrapper() {
  store_->RemoveProvider(child_->config());
}

CertificateProvider* CertificateProviderWrapper::child() const {
  return child_.get();
}

grpc_tls_certificate_distributor* CertificateProviderWrapper::distributor()
    const {
  return distributor_.get();
}

RefCountedPtr<CertificateProviderWrapper>
CertificateProviderStore::CreateOrGetProvider(
    RefCountedPtr<CertificateProviderConfig> key) {
  MutexLock lock(&mu_);
  auto it = map_.find(key);
  if (it != map_.end()) {
    return it->second->Ref();
  } else {
    RefCountedPtr<grpc_tls_certificate_distributor> distributor =
        MakeRefCounted<grpc_tls_certificate_distributor>();
    CertificateProviderFactory* factory =
        CertificateProviderRegistry::GetFactory(key->name());
    OrphanablePtr<CertificateProvider> provider =
        factory->CreateProvider(key, distributor);
    CertificateProviderWrapper* wrapper = new CertificateProviderWrapper(
        std::move(provider), std::move(distributor), this);
    map_.emplace(key, wrapper);
    return RefCountedPtr<CertificateProviderWrapper>(wrapper);
  }
}

void CertificateProviderStore::RemoveProvider(
    RefCountedPtr<CertificateProviderConfig> key) {
  MutexLock lock(&mu_);
  map_.erase(key);
}

}  // namespace grpc_core
