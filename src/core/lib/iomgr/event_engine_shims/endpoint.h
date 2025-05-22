// Copyright 2022 The gRPC Authors
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
#ifndef GRPC_SRC_CORE_LIB_IOMGR_EVENT_ENGINE_SHIMS_ENDPOINT_H
#define GRPC_SRC_CORE_LIB_IOMGR_EVENT_ENGINE_SHIMS_ENDPOINT_H
#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/util/ref_counted.h"

namespace grpc_event_engine {
namespace experimental {

/// Creates an internal grpc_endpoint struct from an EventEngine Endpoint.
/// Server code needs to create grpc_endpoints after the EventEngine has made
/// connections.
grpc_endpoint* grpc_event_engine_endpoint_create(
    std::unique_ptr<EventEngine::Endpoint> ee_endpoint);

/// Returns true if the passed endpoint is an event engine shim endpoint.
bool grpc_is_event_engine_endpoint(grpc_endpoint* ep);

/// Returns the wrapped event engine endpoint if the given grpc_endpoint is an
/// event engine shim endpoint. Otherwise it returns nullptr.
EventEngine::Endpoint* grpc_get_wrapped_event_engine_endpoint(
    grpc_endpoint* ep);

/// Transfers ownership of the wrapped event engine endpoint if the given
/// grpc_endpoint is an event engine shim endpoint. Otherwise it returns
/// nullptr. If the passed ep wraps an event_engine endpoint, then after this
/// call, the memory location holding by the passed ep is free'ed.
/// Its safe to call this function only when there are no pending reads/writes
/// on the endpoint.
std::unique_ptr<EventEngine::Endpoint> grpc_take_wrapped_event_engine_endpoint(
    grpc_endpoint* ep);

/// Destroys the passed in event engine shim endpoint and schedules the
/// asynchronous execution of the on_release_fd callback. The int pointer fd is
/// set to the underlying endpoint's file descriptor.
void grpc_event_engine_endpoint_destroy_and_release_fd(
    grpc_endpoint* ep, int* fd, grpc_closure* on_release_fd);

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
class EndpointWrapper : public grpc_core::RefCounted<EndpointWrapper> {
 public:
  explicit EndpointWrapper(std::unique_ptr<EventEngine::Endpoint> endpoint)
      : endpoint_(std::move(endpoint)) {}

  std::unique_ptr<EventEngine::Endpoint> take_endpoint() {
    return std::move(endpoint_);
  }

  static absl::string_view ChannelArgName() {
    return "grpc.internal.subchannel_endpoint";
  }
  static int ChannelArgsCompare(const EndpointWrapper* a,
                                const EndpointWrapper* b) {
    return QsortCompare(a, b);
  }

 private:
  std::unique_ptr<EventEngine::Endpoint> endpoint_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_SRC_CORE_LIB_IOMGR_EVENT_ENGINE_SHIMS_ENDPOINT_H
