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

#include "src/core/lib/security/certificate_provider/certificate_provider_registry.h"

#include <string.h>

#include <algorithm>
#include <utility>
#include <vector>

#include <grpc/support/log.h>

namespace grpc_core {

void CertificateProviderRegistry::Builder::RegisterCertificateProviderFactory(
    std::unique_ptr<CertificateProviderFactory> factory) {
  absl::string_view name = factory->name();
  gpr_log(GPR_DEBUG, "registering certificate provider factory for \"%s\"",
          std::string(name).c_str());
  GPR_ASSERT(factories_.emplace(name, std::move(factory)).second);
}

CertificateProviderRegistry CertificateProviderRegistry::Builder::Build() {
  return CertificateProviderRegistry(std::move(factories_));
}

CertificateProviderFactory*
CertificateProviderRegistry::LookupCertificateProviderFactory(
    absl::string_view name) const {
  auto it = factories_.find(name);
  if (it == factories_.end()) return nullptr;
  return it->second.get();
}

}  // namespace grpc_core
