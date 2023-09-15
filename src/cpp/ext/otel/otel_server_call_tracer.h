//
//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CPP_EXT_OTEL_OTEL_SERVER_CALL_TRACER_H
#define GRPC_SRC_CPP_EXT_OTEL_OTEL_SERVER_CALL_TRACER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/resource_quota/arena.h"

namespace grpc {
namespace internal {

class OpenTelemetryServerCallTracerFactory
    : public grpc_core::ServerCallTracerFactory {
 public:
  grpc_core::ServerCallTracer* CreateNewServerCallTracer(
      grpc_core::Arena* arena) override;

  bool IsServerTraced(const grpc_core::ChannelArgs& args) override;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_OTEL_OTEL_SERVER_CALL_TRACER_H
