// Copyright 2022 gRPC Authors
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
#ifndef GRPC_EVENT_ENGINE_POSIX_H
#define GRPC_EVENT_ENGINE_POSIX_H

#include <grpc/support/port_platform.h>

#include <functional>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/port.h>
#include <grpc/event_engine/slice_buffer.h>

#ifdef GRPC_EVENT_ENGINE_POSIX

namespace grpc_event_engine {
namespace experimental {

/// This defines an EventEngine interface that all posix specific event engines
/// must implement.
class PosixEventEngine : public EventEngine {
  class PosixEventEngineEndpoint : public EventEngine::Endpoint {
   public:
    /// Returns the file descriptor associated with the posix endpoint.
    virtual int GetWrappedFd() = 0;
    /// Shutdown the endpoint. After this function call its illegal to invoke
    /// any other methods on the endpoint.
    /// \a release_fd - a pointer to hold the file descriptor associated with
    /// the posix endpoint. If it is not nullptr, then the file descriptor is
    /// not closed while the endpoint is shutdown. The pointer will be updated
    /// to hold the endpoint's assocaited file descriptor.
    /// \a on_release_cb - a callback to be invoked when the endpoint is
    /// shutdown and the file descriptor is released. It should only be invoked
    /// if release_fd is not nullptr.
    virtual void Shutdown(
        int* release_fd,
        absl::AnyInvocable<void(absl::Status)> on_release_cb) = 0;
  };
  /// Creates a PosixEventEngineEndpoint from an fd which is already assumed to
  /// be connected to a remote peer.
  /// \a fd - The connected socket file descriptor.
  /// \a peer_addr - The address of the peer to which the provided fd has been
  /// connected.
  /// \a config - Additional configuration to apply to the endpoint.
  /// \a memory_allocator - The endpoint may use the provided memory allocator
  /// to track memory allocations.
  virtual std::unique_ptr<PosixEventEngineEndpoint> CreateEndpointFromFd(
      int fd, const ResolvedAddress& peer_addr, const EndpointConfig& config,
      MemoryAllocator memory_allocator) = 0;

  /// Listens for incoming connection requests from gRPC clients and initiates
  /// request processing once connections are established.
  class PosixEventEngineListener : public EventEngine::Listener {
   public:
    /// Called when the posix listener has accepted a new client connection.
    /// \a listener_fd - The listening socket fd that accepted the new client
    /// connection.
    /// \a endpoint - The EventEngine endpoint to handle data exchange over the
    /// new client connection.
    /// \a memory_allocation - The callback may use the provided memory
    /// allocator to handle memory allocation operations.
    using PosixAcceptCallback = absl::AnyInvocable<void(
        int listener_fd, std::unique_ptr<Endpoint> endpoint,
        MemoryAllocator memory_allocator)>;
    /// Called when a posix listener bind operation completes. A single bind
    /// operation may trigger creation of multiple listener fds. This callback
    /// should be invoked once on each newly created and bound fd. If the
    /// corresponding bind operation fails for a particular fd, this callback
    /// must be invoked with a absl::FailedPreConditionError status.
    ///
    /// \a listener_fd - The listening socket fd that was bound to the specified
    /// address.
    using OnPosixBindNewFdCallback =
        absl::AnyInvocable<void(absl::StatusOr<int> listener_fd)>;
    /// Bind an address/port to this Listener.
    ///
    /// It is expected that multiple addresses/ports can be bound to this
    /// Listener before Listener::Start has been called. Returns either the
    /// bound port or an appropriate error status. The on_bind_new_fd callback
    /// is invoked once for each newly bound listener fd that may be created by
    /// this Bind operation.
    virtual absl::StatusOr<int> Bind(
        const ResolvedAddress& addr,
        OnPosixBindNewFdCallback on_bind_new_fd) = 0;
  };

  /// Factory method to create a network listener / server.
  ///
  /// Once a \a Listener is created and started, the \a on_accept callback will
  /// be called once asynchronously for each established connection. This method
  /// may return a non-OK status immediately if an error was encountered in any
  /// synchronous steps required to create the Listener. In this case,
  /// \a on_shutdown will never be called.
  ///
  /// If this method returns a Listener, then \a on_shutdown will be invoked
  /// exactly once, when the Listener is shut down. The status passed to it will
  /// indicate if there was a problem during shutdown.
  ///
  /// The provided \a MemoryAllocatorFactory is used to create \a
  /// MemoryAllocators for Endpoint construction.
  virtual absl::StatusOr<std::unique_ptr<PosixEventEngineListener>>
  CreateListener(
      PosixEventEngineListener::PosixAcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) = 0;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_POSIX
#endif  // GRPC_EVENT_ENGINE_POSIX_H