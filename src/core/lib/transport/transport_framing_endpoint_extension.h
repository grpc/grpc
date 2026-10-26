// Copyright 2024 The gRPC Authors
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

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_TRANSPORT_FRAMING_ENDPOINT_EXTENSION_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_TRANSPORT_FRAMING_ENDPOINT_EXTENSION_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/slice/slice_buffer.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

/// An Endpoint extension class that will be supported by EventEngine endpoints
/// which can send data to a transport and receive data from it.
class TransportFramingEndpointExtension {
 public:
  virtual ~TransportFramingEndpointExtension() = default;
  static absl::string_view EndpointExtensionName() {
    return "io.grpc.transport.extension.transport_framing_endpoint_"
           "extension";
  }

  // Registers a callback for the endpoint to send a single frame's payload
  // over the transport. Ownership of `data` is transferred by value to the
  // callback, which may be invoked from any thread. Passing `nullptr`
  // unregisters the callback when the transport is closing. After that call
  // returns, implementations must not start new invocations of the previous
  // callback and should discard unsent frames. An invocation already in
  // progress may still complete, so the callback must tolerate being called
  // while the transport is closing.
  virtual void SetSendFrameCallback(
      absl::AnyInvocable<void(SliceBuffer data)> cb) = 0;

  /// Receive data from transport. The data will be from a single frame.
  virtual void ReceiveFrame(SliceBuffer data) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_TRANSPORT_FRAMING_ENDPOINT_EXTENSION_H
