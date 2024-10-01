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

#include "src/core/lib/security/certificate_provider/certificate_provider_registry.h"

#include <grpc/support/port_platform.h>

#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"

namespace grpc_core {

void CertificateProviderRegistry::Builder::RegisterCertificateProviderFactory(
    std::unique_ptr<CertificateProviderFactory> factory) {
  absl::string_view name = factory->name();
  VLOG(2) << "registering certificate provider factory for \"" << name << "\"";
  CHECK(factories_.emplace(name, std::move(factory)).second);
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
