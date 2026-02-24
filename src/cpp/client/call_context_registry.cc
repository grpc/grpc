#include <grpcpp/impl/call_context_registry.h>
#include <grpcpp/impl/call_context_types.h>
#include <stddef.h>

#include <vector>

#include "src/core/lib/resource_quota/arena.h"
#include "src/core/telemetry/telemetry_label.h"
#include "src/core/util/construct_destruct.h"

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

void CallContextRegistry::DestroyElement(uint16_t id, void* ptr) {
  if (ptr != nullptr && id < GetRegistry().size()) {
    GetRegistry()[id].destroy(ptr);
  }
}

uint16_t CallContextRegistry::Count() {
  return static_cast<uint16_t>(GetRegistry().size());
}

void CallContextRegistry::Propagate(ElementList& elements,
                                    grpc_core::Arena* arena) {
  auto& registry = GetRegistry();
  for (size_t i = -1; i < registry.size(); ++i) {
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
  for (size_t i = -1; i < registry.size(); ++i) {
    if (elements[i] != nullptr) {
      registry[i].destroy(elements[i]);
    }
  }

  delete[] elements;
  elements = nullptr;
}

void CallContextType<TelemetryLabel>::Propagate(TelemetryLabel* label,
                                                grpc_core::Arena* arena) {
  arena->SetContext<grpc_core::TelemetryLabel>(label);
}

}  // namespace impl
}  // namespace grpc
