//
//
// Copyright 2015 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_CHANNEL_CONTEXT_H
#define GRPC_SRC_CORE_LIB_CHANNEL_CONTEXT_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/promise/context.h"

/// Call object context pointers.

/// Call context is represented as an array of \a grpc_call_context_elements.
/// This enum represents the indexes into the array, where each index
/// contains a different type of value.
typedef enum {
  /// Value is either a \a grpc_client_security_context or a
  /// \a grpc_server_security_context.
  GRPC_CONTEXT_SECURITY = 0,

  /// Value is a \a census_context.
  GRPC_CONTEXT_TRACING,

  /// Value is a CallTracerAnnotationInterface. (ClientCallTracer object on the
  /// client-side call, or ServerCallTracer on the server-side.)
  GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE,

  /// Value is a CallTracerInterface (ServerCallTracer on the server-side,
  /// CallAttemptTracer on a subchannel call.)
  /// TODO(yashykt): Maybe come up with a better name. This will go away in the
  /// future anyway, so not super important.
  GRPC_CONTEXT_CALL_TRACER,

  /// Reserved for traffic_class_context.
  GRPC_CONTEXT_TRAFFIC,

  /// Holds a pointer to ServiceConfigCallData associated with this call.
  GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA,

  /// Holds a pointer to BackendMetricProvider associated with this call on
  /// the server.
  GRPC_CONTEXT_BACKEND_METRIC_PROVIDER,

  /// Special Google context
  GRPC_CONTEXT_GOOGLE,

  GRPC_CONTEXT_COUNT
} grpc_context_index;

struct grpc_call_context_element {
  void* value = nullptr;
  void (*destroy)(void*) = nullptr;
};

namespace grpc_core {
// Bind the legacy context array into the new style structure
// TODO(ctiller): remove as we migrate these contexts to the new system.
template <>
struct ContextType<grpc_call_context_element> {};
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_CHANNEL_CONTEXT_H
