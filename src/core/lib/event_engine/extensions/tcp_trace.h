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

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"

#include "src/core/lib/channel/tcp_tracer.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

class Http2TransportTcpTracer : public grpc_core::TcpTracerInterface {
 public:
  Http2TransportTcpTracer() {}
  ~Http2TransportTcpTracer() override {}
  // Records a per-message event, unused.
  void RecordEvent(Type /*type*/, absl::Time /*time*/, size_t /*byte_offset*/,
                   absl::optional<ConnectionMetrics> /*metrics*/) override {}
  // Records per-connection metrics.
  void RecordConnectionMetrics(ConnectionMetrics metrics) override;

 private:
  grpc_core::Mutex mu_;
  ConnectionMetrics connection_metrics_ ABSL_GUARDED_BY(mu_);
};

class TcpTraceExtension {
 public:
  virtual ~TcpTraceExtension() = default;
  static absl::string_view EndpointExtensionName() {
    return "io.grpc.event_engine.extension.tcp_trace";
  }
  virtual void SetTcpTracer(
      std::shared_ptr<grpc_core::TcpTracerInterface> tcp_tracer) = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_EXTENSIONS_TCP_TRACE_H
