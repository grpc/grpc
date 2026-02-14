#ifndef GRPCPP_IMPL_CALL_CONTEXT_TYPES_H
#define GRPCPP_IMPL_CALL_CONTEXT_TYPES_H

#include <string_view>
#include <grpcpp/impl/call_context_registry.h>

namespace grpc {
namespace impl {

struct TelemetryLabel {
  std::string value;
};

template <>
struct CallContextType<TelemetryLabel> : public CallContextTypeBase<TelemetryLabel> {
  static void Propagate(const TelemetryLabel* public_opt, grpc_core::Arena* arena);
};

}  // namespace impl
}  // namespace grpc

#endif  // GRPCPP_IMPL_CALL_CONTEXT_TYPES_H