// Copyright 2026 gRPC authors.
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

#include <grpcpp/call_context_types.h>
#include <grpcpp/impl/allowed_call_context_types.h>
#include <grpcpp/impl/call_context_registry.h>
#include <stddef.h>

#include <cstdint>
#include <vector>

#include "src/core/lib/resource_quota/arena.h"
#include "src/core/telemetry/telemetry_label.h"  // IWYU pragma: keep
#include "src/core/util/no_destruct.h"

namespace grpc {
namespace impl {

struct RegistryEntry {
  void (*destroy)(void*);
  void (*propagate)(void*, grpc_core::Arena*);
};

static std::vector<RegistryEntry>& GetRegistry() {
  static grpc_core::NoDestruct<std::vector<RegistryEntry>> registry;
  return *registry;
}

uint16_t CallContextRegistry::Register(void (*d)(void*),
                                       void (*p)(void*, grpc_core::Arena*)) {
  auto& registry = GetRegistry();
  uint16_t id = static_cast<uint16_t>(registry.size());
  registry.push_back({d, p});
  return id;
}

void CallContextRegistry::DestroyElement(uint16_t id, void* element) {
  if (element != nullptr && id < GetRegistry().size()) {
    GetRegistry()[id].destroy(element);
  }
}

uint16_t CallContextRegistry::Count() {
  return static_cast<uint16_t>(GetRegistry().size());
}

void CallContextRegistry::Propagate(ElementList& elements,
                                    grpc_core::Arena* arena) {
  if (elements == nullptr) return;
  auto& registry = GetRegistry();
  for (size_t i = 0; i < registry.size(); ++i) {
    if (elements[i] != nullptr) {
      registry[i].propagate(elements[i], arena);
    }
  }
  delete[] elements;
  elements = nullptr;
}

void CallContextRegistry::Destroy(ElementList& elements) {
  if (elements == nullptr) return;
  auto& registry = GetRegistry();
  for (size_t i = 0; i < registry.size(); ++i) {
    if (elements[i] != nullptr) {
      registry[i].destroy(elements[i]);
    }
  }
  delete[] elements;
  elements = nullptr;
}

void CallContextType<TelemetryLabel>::Propagate(TelemetryLabel* label,
                                                grpc_core::Arena* arena) {
  arena->SetContext<grpc_core::TelemetryLabel>(
      arena->ManagedNew<grpc_core::TelemetryLabel>(std::move(*label)));
  delete label;
}

}  // namespace impl
}  // namespace grpc
