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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_LISTENER_UTILS_H
#define GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_LISTENER_UTILS_H

#include <grpc/support/port_platform.h>

#include <functional>
#include <string>
#include <utility>

#include "absl/status/statusor.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"

namespace grpc_event_engine {
namespace posix_engine {

// This interface exists to allow different Event Engines to implement different
// custom interception operations while a socket is Appended/Erased. The
// listener util functions are defined over this interface and thus can be
// shared across multiple event engines.
class ListenerSocketsContainer {
 public:
  struct ListenerSocket {
    // Listener socket fd
    PosixSocketWrapper sock;
    // Assigned/chosen listening port
    int port;
    // Socket configuration
    bool zero_copy_enabled;
    // Address at which the socket is listening for connections
    grpc_event_engine::experimental::EventEngine::ResolvedAddress addr;
    // Dual stack mode.
    PosixSocketWrapper::DSMode dsmode;
  };
  // Adds a socket to the internal db of sockets associated with a listener.
  virtual void Append(ListenerSocket socket) = 0;

  // Returns a non-OK status if the socket cannot be found. Otherwise, returns
  // the socket.
  virtual absl::StatusOr<ListenerSocket> Find(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
          addr) = 0;

  virtual ~ListenerSocketsContainer() = default;
};

// If successful, add a socket to ListenerSocketsContainer for \a addr, set \a
// dsmode for the socket, and return the error handle and listening port
// assigned for the socket.
absl::StatusOr<ListenerSocketsContainer::ListenerSocket>
CreateAndPrepareListenerSocket(
    const PosixTcpOptions& options,
    const grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr);

// Instead of creating and adding a socket bound to specific address, this
// function creates and adds a socket bound to the wildcard address on the
// server. Returns the port at which the created socket listens for incoming
// connections.
absl::StatusOr<int> ListenerContainerAddWildcardAddresses(
    ListenerSocketsContainer& listener_sockets, const PosixTcpOptions& options,
    int requested_port);

// Get all addresses assigned to network interfaces on the machine and create a
// socket for each. requested_port is the port to use for every socket, or 0 to
// select one random port that will be used for every socket. Return
// assigned_port.
absl::StatusOr<int> ListenerContainerAddAllLocalAddresses(
    ListenerSocketsContainer& listener_sockets, const PosixTcpOptions& options,
    int requested_port);

}  // namespace posix_engine
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_LISTENER_UTILS_H
