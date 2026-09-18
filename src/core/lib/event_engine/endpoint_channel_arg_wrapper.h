// Copyright 2025 The gRPC Authors
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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_ENDPOINT_CHANNEL_ARG_WRAPPER_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_ENDPOINT_CHANNEL_ARG_WRAPPER_H

#include <grpc/event_engine/event_engine.h>

#include <memory>

#include "src/core/util/ref_counted.h"

namespace grpc_event_engine {

namespace experimental {

/// Wrapper for EventEngine::Endpoint to enable storing it in channel args.
///
/// This class encapsulates a `std::unique_ptr<EventEngine::Endpoint>` so that
/// an already-connected endpoint can be passed through channel arguments.
/// This is useful when creating a channel with a pre-established connection,
/// such as when using `CreateChannelFromEndpoint()` or `CreateChannelFromFd()`.
///
/// The wrapper provides:
/// - Ownership management via unique_ptr
/// - A static `ChannelArgName()` method to identify the channel arg
/// - A comparison function for use in channel args internals
///
/// Note: This is intended for internal use only.
class EndpointChannelArgWrapper
    : public grpc_core::RefCounted<EndpointChannelArgWrapper> {
 public:
  explicit EndpointChannelArgWrapper(
      std::unique_ptr<EventEngine::Endpoint> endpoint);

  std::unique_ptr<EventEngine::Endpoint> TakeEndpoint();

  static absl::string_view ChannelArgName();
  static int ChannelArgsCompare(const EndpointChannelArgWrapper* a,
                                const EndpointChannelArgWrapper* b);

 private:
  std::unique_ptr<EventEngine::Endpoint> endpoint_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_ENDPOINT_CHANNEL_ARG_WRAPPER_H
