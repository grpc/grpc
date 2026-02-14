#ifndef GRPC_SRC_CORE_TELEMETRY_TELEMETRY_LABEL_H
#define GRPC_SRC_CORE_TELEMETRY_TELEMETRY_LABEL_H

#include <string_view>
#include "src/core/lib/resource_quota/arena.h"

namespace grpc_core {

struct TelemetryLabel {
  std::string_view value;
};

template <>
struct ArenaContextType<TelemetryLabel> {
  static void Destroy(TelemetryLabel*) {}
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_TELEMETRY_TELEMETRY_LABEL_H