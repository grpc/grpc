// Copyright 2026 The gRPC Authors
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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_EXTENSIONS_RECEIVE_COALESCING_EXTENSION_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_EXTENSIONS_RECEIVE_COALESCING_EXTENSION_H

#include "absl/strings/string_view.h"

namespace grpc_event_engine::experimental {

/// An Endpoint extension class that will be supported by EventEngine endpoints
/// which need to work with receive coalescing.
class ReceiveCoalescingExtension {
 public:
  virtual ~ReceiveCoalescingExtension() = default;
  static absl::string_view EndpointExtensionName() {
    return "io.grpc.event_engine.extension.receive_coalescing_extension";
  }

  /// Forces the endpoint to receive rpcs in one contiguous block of memory.
  /// It is safe to call this only when there are no outstanding Reads on
  /// the Endpoint.
  virtual void EnableRpcReceiveCoalescing() = 0;

  /// Disables rpc receive coalescing until it is explicitly enabled again.
  /// It is safe to call this only when there are no outstanding Reads on
  /// the Endpoint.
  virtual void DisableRpcReceiveCoalescing() = 0;
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_EXTENSIONS_RECEIVE_COALESCING_EXTENSION_H
