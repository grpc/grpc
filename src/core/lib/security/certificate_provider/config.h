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

#ifndef GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_CONFIG_H
#define GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_CONFIG_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

// Interface for configs for CertificateProviders. Each plugin will implement
// the methods according to their need to allow sharing CertificateProvider
// instances that have equivalent configurations.
class CertificateProviderConfig : public RefCounted<CertificateProviderConfig> {
 public:
  CertificateProviderConfig(const char* name, const Json& json)
      : config_json_str_(std::string(name) + json.Dump()) {}
  virtual ~CertificateProviderConfig() {}

  // Name of the type of the CertificateProvider. Unique to each type of config.
  virtual const char* name() const = 0;

  // Return the hash of the config. When computing the hash for a config, the
  // implementations must take the name of the plugin as an input of the hash
  // function.
  size_t hash() const { return std::hash<std::string>()(config_json_str_); }

  // Compare if two configs are of the same type and are equivalent to each
  // other.
  bool operator==(const CertificateProviderConfig& rhs) const {
    return rhs.config_json_str_ == config_json_str_;
  }

 private:
  std::string config_json_str_;
};

// Hasher for the configs.
class CertificateProviderConfigHasher {
 public:
  size_t operator()(const RefCountedPtr<CertificateProviderConfig>& obj) const {
    return obj->hash();
  }
};

// Comparing two pointed CertificateProviderConfig objects.
class CertificateProviderConfigPred {
 public:
  size_t operator()(const RefCountedPtr<CertificateProviderConfig>& lhs,
                    const RefCountedPtr<CertificateProviderConfig>& rhs) const {
    return *lhs.get() == *rhs.get();
  }
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_CONFIG_H
