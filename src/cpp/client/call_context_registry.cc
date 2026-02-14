#include <grpcpp/impl/call_context_registry.h>
#include <grpcpp/impl/call_context_types.h>
#include <stddef.h>
#include <vector>

#include "src/core/lib/resource_quota/arena.h"
#include "src/core/telemetry/telemetry.h" 

namespace grpc {
namespace impl {

struct RegistryEntry {
  void (*destroy)(void*);
  void (*propagate)(void*, grpc_core::Arena*);
};

static std::vector<RegistryEntry>& GetRegistry() {
  static auto* registry = new std::vector<RegistryEntry>();
  return *registry;
}

uint16_t CallContextRegistry::Register(void (*d)(void*), 
                                       void (*p)(void*, grpc_core::Arena*)) {
  auto& registry = GetRegistry();
  uint16_t id = static_cast<uint16_t>(registry.size());
  registry.push_back({d, p});
  return id;
}

void CallContextRegistry::Destroy(uint16_t id, void* ptr) {
  if (ptr && id < GetRegistry().size()) {
    GetRegistry()[id].destroy(ptr);
  }
}

void CallContextRegistry::PropagateAll(void** context_types, grpc_core::Arena* arena) {
  auto& registry = GetRegistry();
  for (size_t i = 0; i < registry.size(); ++i) {
    if (context_types[i]) {
      registry[i].propagate(context_types[i], arena);
    }
  }
}

uint16_t CallContextRegistry::Count() {
  return static_cast<uint16_t>(GetRegistry().size());
}

// Propagation for whitelisted types
void CallContextType<TelemetryLabel>::Propagate(const TelemetryLabel* public_label, 
                                                grpc_core::Arena* arena) {
  auto* arena_label = arena->New<grpc_core::TelemetryLabel>();
  arena_label->value = public_label->value
  arena->SetContext<grpc_core::TelemetryLabel>(arena_label);
}

}  // namespace impl
}  // namespace grpc