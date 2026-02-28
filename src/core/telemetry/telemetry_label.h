#ifndef GRPC_SRC_CORE_TELEMETRY_TELEMETRY_LABEL_H
#define GRPC_SRC_CORE_TELEMETRY_TELEMETRY_LABEL_H

#include <grpc/context_types.h>

#include "src/core/lib/resource_quota/arena.h"

namespace grpc_core {

template <>
struct ArenaContextType<TelemetryLabel> {
  static void Destroy(TelemetryLabel* l) { delete l; }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TELEMETRY_TELEMETRY_LABEL_H
