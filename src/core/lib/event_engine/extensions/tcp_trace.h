// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_EXTENSIONS_TCP_TRACE_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_EXTENSIONS_TCP_TRACE_H

#include <memory>

#include "src/core/telemetry/instrument.h"
#include "src/core/telemetry/tcp_tracer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/strings/string_view.h"

namespace grpc_event_engine::experimental {

class TcpTraceExtension {
 public:
  virtual ~TcpTraceExtension() = default;
  static absl::string_view EndpointExtensionName() {
    return "io.grpc.event_engine.extension.tcp_trace";
  }
  virtual void SetTcpTracer(
      std::shared_ptr<grpc_core::TcpConnectionTracer> tracer) = 0;

  // Enable TCP telemetry collection using the Instrumentation API.
  virtual void EnableTcpTelemetry(
      grpc_core::RefCountedPtr<grpc_core::CollectionScope> collection_scope,
      bool is_control_endpoint) = 0;
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_EXTENSIONS_TCP_TRACE_H
