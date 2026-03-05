#ifndef GRPCPP_IMPL_ALLOWED_CALL_CONTEXT_TYPES_H
#define GRPCPP_IMPL_ALLOWED_CALL_CONTEXT_TYPES_H

#include <grpcpp/call_context_types.h>
#include <grpcpp/impl/call_context_registry.h>

namespace grpc {
namespace impl {

template <>
struct CallContextType<TelemetryLabel>
    : public CallContextTypeBase<TelemetryLabel> {
  static void Propagate(TelemetryLabel* label, grpc_core::Arena* arena);
};

}  // namespace impl
}  // namespace grpc

#endif  // GRPCPP_IMPL_ALLOWED_CALL_CONTEXT_TYPES_H
