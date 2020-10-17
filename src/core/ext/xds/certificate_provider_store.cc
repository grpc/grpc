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

namespace grpc_core {

RefCountedPtr<grpc_tls_certificate_provider>
CertificateProviderStore::CreateOrGetCertificateProvider(
    absl::string_view key) {
  MutexLock lock(&mu_);
  auto it = certificate_providers_map_.find(key);
  if (it != certificate_providers_map_.end()) {
    it->second.refs.Ref();
    return it->second.certificate_provider;
  }
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
  RefCountedPtr<grpc_tls_certificate_provider> cert_provider =
      factory->CreateCertificateProvider(plugin_config_it->second.config);
  // We need to use emplace and forward arguments since
  // CertificateProviderWrapper is non-copyable and non-movable.
  certificate_providers_map_.emplace(
      std::piecewise_construct, std::forward_as_tuple(plugin_config_it->first),
      std::forward_as_tuple(cert_provider));
  return cert_provider;
}

void CertificateProviderStore::ReleaseCertificateProvider(
    absl::string_view key) {
  MutexLock lock(&mu_);
  auto it = certificate_providers_map_.find(key);
  if (it != certificate_providers_map_.end()) {
    if (it->second.refs.Unref()) {
      certificate_providers_map_.erase(it);
    }
  }
}

}  // namespace grpc_core
