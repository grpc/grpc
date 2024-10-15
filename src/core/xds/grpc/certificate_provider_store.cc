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

#include "src/core/xds/grpc/certificate_provider_store.h"

#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/security/certificate_provider/certificate_provider_registry.h"

namespace grpc_core {

//
// CertificateProviderStore::PluginDefinition
//

const JsonLoaderInterface*
CertificateProviderStore::PluginDefinition::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<PluginDefinition>()
          .Field("plugin_name", &PluginDefinition::plugin_name)
          .Finish();
  return loader;
}

void CertificateProviderStore::PluginDefinition::JsonPostLoad(
    const Json& json, const JsonArgs& args, ValidationErrors* errors) {
  // Check that plugin is supported.
  CertificateProviderFactory* factory = nullptr;
  if (!plugin_name.empty()) {
    ValidationErrors::ScopedField field(errors, ".plugin_name");
    factory = CoreConfiguration::Get()
                  .certificate_provider_registry()
                  .LookupCertificateProviderFactory(plugin_name);
    if (factory == nullptr) {
      errors->AddError(absl::StrCat("Unrecognized plugin name: ", plugin_name));
      return;  // No point checking config.
    }
  }
  // Parse the config field.
  {
    ValidationErrors::ScopedField field(errors, ".config");
    auto it = json.object().find("config");
    // The config field is optional; if not present, we use an empty JSON
    // object.
    Json::Object config_json;
    if (it != json.object().end()) {
      if (it->second.type() != Json::Type::kObject) {
        errors->AddError("is not an object");
        return;  // No point parsing config.
      } else {
        config_json = it->second.object();
      }
    }
    if (factory == nullptr) return;
    // Use plugin to validate and parse config.
    config = factory->CreateCertificateProviderConfig(
        Json::FromObject(std::move(config_json)), args, errors);
  }
}

//
// CertificateProviderStore::CertificateProviderWrapper
//

UniqueTypeName CertificateProviderStore::CertificateProviderWrapper::type()
    const {
  static UniqueTypeName::Factory kFactory("Wrapper");
  return kFactory.Create();
}

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
    result =
        it->second->RefIfNonZero().TakeAsSubclass<CertificateProviderWrapper>();
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
      CoreConfiguration::Get()
          .certificate_provider_registry()
          .LookupCertificateProviderFactory(
              plugin_config_it->second.plugin_name);
  if (factory == nullptr) {
    // This should never happen since an entry is only inserted in the
    // plugin_config_map_ if the corresponding factory was found when parsing
    // the xDS bootstrap file.
    LOG(ERROR) << "Certificate provider factory "
               << plugin_config_it->second.plugin_name << " not found";
    return nullptr;
  }
  return MakeRefCounted<CertificateProviderWrapper>(
      factory->CreateCertificateProvider(plugin_config_it->second.config),
      Ref(), plugin_config_it->first);
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

}  // namespace grpc_core
