// Copyright 2023 The gRPC Authors
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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_WINDOWS_LISTENER_H
#define GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_WINDOWS_LISTENER_H

#include <grpc/support/port_platform.h>

#include <list>

#include "absl/status/statusor.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_event_engine {
namespace experimental {

class WindowsEventEngineListener : public EventEngine::Listener {
 public:
  WindowsEventEngineListener(
      IOCP* iocp, AcceptCallback accept_cb,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory,
      Executor* executor, const EndpointConfig& config);
  ~WindowsEventEngineListener() override;
  absl::StatusOr<int> Bind(const EventEngine::ResolvedAddress& addr) override;
  absl::Status Start() override;

 private:
  friend class SinglePortSocketListener;
  /// Responsible for listening on a single port.
  class SinglePortSocketListener {
   public:
    ~SinglePortSocketListener();
    // This factory will create a bound, listening WinSocket, registered with
    // the listener's IOCP poller.
    static absl::StatusOr<std::unique_ptr<SinglePortSocketListener>> Create(
        WindowsEventEngineListener* listener, SOCKET sock,
        EventEngine::ResolvedAddress addr);

    // Two-stage initialization, allows creation of all bound sockets before the
    // listener is started.
    absl::Status Start();
    absl::Status StartLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

    // Accessor methods
    EventEngine::ResolvedAddress listener_sockname() {
      return listener_sockname_;
    };
    int port() { return port_; }
    WinSocket* listener_socket() { return listener_socket_.get(); }

   private:
    SinglePortSocketListener(WindowsEventEngineListener* listener,
                             LPFN_ACCEPTEX AcceptEx,
                             std::unique_ptr<WinSocket> win_socket, int port);

    // Bind a recently-created socket for listening
    static absl::StatusOr<int> PrepareListenerSocket(
        SOCKET sock, const EventEngine::ResolvedAddress& addr);

    void OnAcceptCallbackImpl();
    void DecrementActivePortsAndNotifyLocked();

    // The cached AcceptEx for that port.
    LPFN_ACCEPTEX AcceptEx;
    // This seemingly magic number comes from AcceptEx's documentation. each
    // address buffer needs to have at least 16 more bytes at their end.
    uint8_t addresses_[(sizeof(sockaddr_in6) + 16) * 2] = {};
    // The parent listener
    WindowsEventEngineListener* listener_;
    // closure for socket notification of accept being ready
    AnyInvocableClosure on_accept_;
    // The actual TCP port number.
    int port_;
    // Syncronize accept handling on the same socket.
    grpc_core::Mutex mu_;
    // This will hold the socket for the next accept.
    SOCKET accept_socket_ ABSL_GUARDED_BY(mu_) = INVALID_SOCKET;
    // The listener winsocket.
    std::unique_ptr<WinSocket> listener_socket_ ABSL_GUARDED_BY(mu_);
    EventEngine::ResolvedAddress listener_sockname_;
  };
  absl::StatusOr<SinglePortSocketListener*> AddSinglePortSocketListener(
      SOCKET sock, EventEngine::ResolvedAddress addr);

  // DO NOT SUBMIT(hork): is this necessary?
  void Shutdown();

  IOCP* iocp_;
  AcceptCallback accept_cb_;
  absl::AnyInvocable<void(absl::Status)> on_shutdown_;
  bool started_{false};
  std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory_;
  grpc_core::Mutex socket_listeners_mu_;
  std::list<std::unique_ptr<SinglePortSocketListener>> port_listeners_
      ABSL_GUARDED_BY(socket_listeners_mu_);
  const EndpointConfig& config_;
  Executor* executor_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_WINDOWS_WINDOWS_LISTENER_H