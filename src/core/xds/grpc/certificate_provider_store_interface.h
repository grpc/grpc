//
// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_GRPC_CERTIFICATE_PROVIDER_STORE_INTERFACE_H
#define GRPC_SRC_CORE_XDS_GRPC_CERTIFICATE_PROVIDER_STORE_INTERFACE_H

#include <map>
#include <string>

#include "src/core/credentials/transport/tls/certificate_provider_factory.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/validation_errors.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Map for xDS based grpc_tls_certificate_provider instances.
class CertificateProviderStoreInterface
    : public RefCounted<CertificateProviderStoreInterface> {
 public:
  struct PluginDefinition {
    std::string plugin_name;
    RefCountedPtr<CertificateProviderFactory::Config> config;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
    void JsonPostLoad(const Json& json, const JsonArgs& args,
                      ValidationErrors* errors);
  };

  // Maps plugin instance (opaque) name to plugin definition.
  using PluginDefinitionMap = std::map<std::string, PluginDefinition>;

  // If a certificate provider corresponding to the instance name \a key is
  // found, a ref to the grpc_tls_certificate_provider is returned. If no
  // provider is found for the key, a new provider is created from the plugin
  // definition map.
  // Returns nullptr on failure to get or create a new certificate provider.
  virtual RefCountedPtr<grpc_tls_certificate_provider>
  CreateOrGetCertificateProvider(absl::string_view key) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_CERTIFICATE_PROVIDER_STORE_INTERFACE_H
