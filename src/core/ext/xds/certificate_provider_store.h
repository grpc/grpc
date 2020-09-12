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

#ifndef GRPC_CORE_EXT_XDS_CERTIFICATE_PROVIDER_STORE_H
#define GRPC_CORE_EXT_XDS_CERTIFICATE_PROVIDER_STORE_H

#include <grpc/support/port_platform.h>

#include <map>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/security/certificate_provider.h"

namespace grpc_core {

// Map for xDS based grpc_tls_certificate_provider instances.
class CertificateProviderStore {
 public:
  // If a provider corresponding to the config is found, a raw pointer to the
  // grpc_tls_certificate_provider in the map is returned. If no provider is
  // found for a key, a new provider is created. The CertificateProviderStore
  // maintains a ref to the grpc_tls_certificate_provider for its entire
  // lifetime.
  RefCountedPtr<grpc_tls_certificate_provider> CreateOrGetCertificateProvider(
      absl::string_view key);

 private:
  // Underlying map for the providers.
  std::map<std::string, RefCountedPtr<grpc_tls_certificate_provider>> map_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_CERTIFICATE_PROVIDER_STORE_H
