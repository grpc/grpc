//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_GRPC_GRPC_XDS_DECODE_CONTEXT_H
#define GRPC_SRC_CORE_XDS_GRPC_GRPC_XDS_DECODE_CONTEXT_H

#include "src/core/xds/xds_client/xds_resource_type.h"

namespace grpc_core {

// TODO(roth): Consider moving registries from bootstrap into this interface.
struct GrpcXdsDecodeContext final : public XdsResourceType::DecodeContext {
  enum class Component { kClient, kServer };
  Component component;

  GrpcXdsDecodeContext(XdsClient* client_in,
                       const XdsBootstrap::XdsServer& server_in,
                       upb_DefPool* def_pool_in, upb_Arena* arena_in,
                       Component component_in)
      : DecodeContext(client_in, server_in, def_pool_in, arena_in),
        component(component_in) {}
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_GRPC_XDS_DECODE_CONTEXT_H
