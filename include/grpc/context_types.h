#ifndef GRPC_CONTEXT_TYPES_H
#define GRPC_CONTEXT_TYPES_H

#include <string>

namespace grpc_core {

struct TelemetryLabel {
  std::string value;
};

}  // namespace grpc_core

#endif // GRPC_CONTEXT_TYPES_H
