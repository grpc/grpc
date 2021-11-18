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

#include "src/core/ext/xds/certificate_provider_registry.h"

#include "absl/container/inlined_vector.h"

namespace grpc_core {

namespace {

class RegistryState {
 public:
  void RegisterCertificateProviderFactory(
      std::unique_ptr<CertificateProviderFactory> factory) {
    gpr_log(GPR_DEBUG, "registering certificate provider factory for \"%s\"",
            factory->name());
    for (size_t i = 0; i < factories_.size(); ++i) {
      GPR_ASSERT(strcmp(factories_[i]->name(), factory->name()) != 0);
    }
    factories_.push_back(std::move(factory));
  }

  CertificateProviderFactory* LookupCertificateProviderFactory(
      absl::string_view name) const {
    for (size_t i = 0; i < factories_.size(); ++i) {
      if (name == factories_[i]->name()) {
        return factories_[i].get();
      }
    }
    return nullptr;
  }

 private:
  // We currently support 3 factories without doing additional
  // allocation.  This number could be raised if there is a case where
  // more factories are needed and the additional allocations are
  // hurting performance (which is unlikely, since these allocations
  // only occur at gRPC initialization time).
  absl::InlinedVector<std::unique_ptr<CertificateProviderFactory>, 3>
      factories_;
};

RegistryState* g_state = nullptr;

}  // namespace

//
// CertificateProviderRegistry
//

CertificateProviderFactory*
CertificateProviderRegistry::LookupCertificateProviderFactory(
    absl::string_view name) {
  GPR_ASSERT(g_state != nullptr);
  return g_state->LookupCertificateProviderFactory(name);
}

void CertificateProviderRegistry::InitRegistry() {
  if (g_state == nullptr) g_state = new RegistryState();
}

void CertificateProviderRegistry::ShutdownRegistry() {
  delete g_state;
  g_state = nullptr;
}

void CertificateProviderRegistry::RegisterCertificateProviderFactory(
    std::unique_ptr<CertificateProviderFactory> factory) {
  InitRegistry();
  g_state->RegisterCertificateProviderFactory(std::move(factory));
}

}  // namespace grpc_core

//
// Plugin registration
//

void grpc_certificate_provider_registry_init() {
  grpc_core::CertificateProviderRegistry::InitRegistry();
}

void grpc_certificate_provider_registry_shutdown() {
  grpc_core::CertificateProviderRegistry::ShutdownRegistry();
}
