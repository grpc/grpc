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

#include "src/core/ext/xds/certificate_provider_store.h"

#include "src/core/ext/xds/certificate_provider_registry.h"
#include "src/core/ext/xds/xds_client.h"

namespace grpc_core {

//
// CertificateProviderStore
//

// If a certificate provider is created, the CertificateProviderStore
// maintains a raw pointer to the created CertificateProviderWrapper so that
// future calls to `CreateOrGetCertificateProvider()` with the same key result
// in returning a ref to this created certificate provider. This entry is
// deleted when the refcount to this provider reaches zero.
RefCountedPtr<grpc_tls_certificate_provider>
CertificateProviderStore::CreateOrGetCertificateProvider(
    absl::string_view key) {
  RefCountedPtr<CertificateProviderWrapper> result;
  MutexLock lock(&mu_);
  auto it = certificate_providers_map_.find(key);
  if (it == certificate_providers_map_.end()) {
    result = CreateCertificateProviderLocked(key);
    if (result != nullptr) {
      certificate_providers_map_.insert({result->key(), result.get()});
    }
  } else {
    result = it->second->RefIfNonZero();
    if (result == nullptr) {
      result = CreateCertificateProviderLocked(key);
      it->second = result.get();
    }
  }
  return result;
}

RefCountedPtr<CertificateProviderStore::CertificateProviderWrapper>
CertificateProviderStore::CreateCertificateProviderLocked(
    absl::string_view key) {
  auto plugin_config_it = plugin_config_map_.find(std::string(key));
  if (plugin_config_it == plugin_config_map_.end()) {
    return nullptr;
  }
  CertificateProviderFactory* factory =
      CertificateProviderRegistry::LookupCertificateProviderFactory(
          plugin_config_it->second.plugin_name);
  if (factory == nullptr) {
    // This should never happen since an entry is only inserted in the
    // plugin_config_map_ if the corresponding factory was found when parsing
    // the xDS bootstrap file.
    gpr_log(GPR_ERROR, "Certificate provider factory %s not found",
            plugin_config_it->second.plugin_name.c_str());
    return nullptr;
  }
  return MakeRefCounted<CertificateProviderWrapper>(
      factory->CreateCertificateProvider(plugin_config_it->second.config),
      xds_client_ == nullptr ? nullptr : xds_client_->Ref(), this,
      plugin_config_it->first);
}

void CertificateProviderStore::ReleaseCertificateProvider(
    absl::string_view key, CertificateProviderWrapper* wrapper) {
  MutexLock lock(&mu_);
  auto it = certificate_providers_map_.find(key);
  if (it != certificate_providers_map_.end()) {
    if (it->second == wrapper) {
      certificate_providers_map_.erase(it);
    }
  }
}

//
// CertificateProviderStore::CertificateProviderWrapper
//

CertificateProviderStore::CertificateProviderWrapper::
    CertificateProviderWrapper(
        RefCountedPtr<grpc_tls_certificate_provider> certificate_provider,
        RefCountedPtr<XdsClient> xds_client, CertificateProviderStore* store,
        absl::string_view key)
    : certificate_provider_(std::move(certificate_provider)),
      xds_client_(std::move(xds_client)),
      store_(store),
      key_(key) {}

CertificateProviderStore::CertificateProviderWrapper::
    ~CertificateProviderWrapper() {
  store_->ReleaseCertificateProvider(key_, this);
}

}  // namespace grpc_core
