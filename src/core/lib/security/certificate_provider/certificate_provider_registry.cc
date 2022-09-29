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
  gpr_log(GPR_DEBUG, "registering certificate provider factory for \"%s\"",
          factory->name());
  for (size_t i = 0; i < factories_.size(); ++i) {
    GPR_ASSERT(strcmp(factories_[i]->name(), factory->name()) != 0);
  }
  factories_.push_back(std::move(factory));
}

CertificateProviderRegistry CertificateProviderRegistry::Builder::Build() {
  CertificateProviderRegistry r;
  r.factories_ = std::move(factories_);
  return r;
}

CertificateProviderFactory*
CertificateProviderRegistry::LookupCertificateProviderFactory(
    absl::string_view name) const {
  for (size_t i = 0; i < factories_.size(); ++i) {
    if (name == factories_[i]->name()) {
      return factories_[i].get();
    }
  }
  return nullptr;
}

}  // namespace grpc_core
