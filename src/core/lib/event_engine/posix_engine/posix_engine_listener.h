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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_LISTENER_H
#define GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_LISTENER_H

#include <functional>
#include <list>
#include <string>
#include <utility>

#include "absl/status/statusor.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/posix_engine/event_poller.h"
#include "src/core/lib/event_engine/posix_engine/posix_engine_listener_utils.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"

namespace grpc_event_engine {
namespace posix_engine {

class PosixEngineListener final : public EventEngine::Listener {
 public:
  explicit PosixEngineListener(
      EventEngine::Listener::AcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      const grpc_event_engine::experimental::EndpointConfig& config,
      std::unique_ptr<grpc_event_engine::experimental::MemoryAllocatorFactory>
          memory_allocator_factory,
      PosixEventPoller* poller, std::shared_ptr<EventEngine> engine);
  // Binds an address to the listener. This creates a ListenerSocket
  // and sets its fields appropriately.
  absl::StatusOr<int> Bind(const EventEngine::ResolvedAddress& addr) final;
  // Signals event manager to listen for connections on all created sockets.
  absl::Status Start() final;

  static void NotifyOnAccept(ListenerSocketsContainer::ListenerSocket* socket);

  ~PosixEngineListener() override;

 private:
  class PosixEngineListenerSocketsContainer : public ListenerSocketsContainer {
   public:
    explicit PosixEngineListenerSocketsContainer(PosixEngineListener* listener)
        : listener_(listener){};
    void Append(ListenerSocket socket) override { sockets_.push_back(socket); }

    absl::StatusOr<ListenerSocket> Find(
        const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
            addr) override {
      for (auto socket = sockets_.begin(); socket != sockets_.end(); ++socket) {
        if (socket->addr.size() == addr.size() &&
            memcmp(socket->addr.address(), addr.address(), addr.size()) == 0) {
          return *socket;
        }
      }
      return absl::NotFoundError("Socket not found!");
    }

    void Erase(int /*fd*/) override { GPR_ASSERT(false && "unimplemented"); }

    int Size() { return static_cast<int>(sockets_.size()); }

    std::list<ListenerSocket>::const_iterator begin() {
      return sockets_.begin();
    }
    std::list<ListenerSocket>::const_iterator end() { return sockets_.end(); }

   private:
    std::list<ListenerSocket> sockets_;
    PosixEngineListener* listener_;
  };
  friend class PosixEngineListenerSocketsContainer;
  // The mutex ensures thread safety when multiple threads try to call Bind
  // and Start in parallel.
  absl::Mutex mu_;
  std::shared_ptr<EventEngine> engine_;
  // Linked list of sockets. One is created upon each successful bind
  // operation.
  PosixEngineListenerSocketsContainer sockets_ ABSL_GUARDED_BY(mu_);
  // Callback to be invoked upon accepting a connection.
  EventEngine::Listener::AcceptCallback on_accept_;
  // Callback to be invoked upon shutdown of listener.
  absl::AnyInvocable<void(absl::Status)> on_shutdown_;
  PosixEventPoller* poller_;
  std::shared_ptr<EventEngine> event_engine_;
  PosixTcpOptions options_;
  // Set to true when the listener has started listening for new connections.
  // Any further bind operations would fail.
  bool started_ ABSL_GUARDED_BY(mu_) = false;
  // Pointer to a slice allocator factory object which can generate
  // unique slice allocators for each new incoming connection.
  std::unique_ptr<grpc_event_engine::experimental::MemoryAllocatorFactory>
      memory_allocator_factory_;
};

}  // namespace posix_engine
}  // namespace grpc_event_engine
#endif  // GRPC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_ENGINE_LISTENER_H