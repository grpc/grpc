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
#ifndef GRPC_SRC_CPP_SERVER_PASSIVE_LISTENER_INTERNAL_H
#define GRPC_SRC_CPP_SERVER_PASSIVE_LISTENER_INTERNAL_H

#include <grpc/support/port_platform.h>

#include <grpc/passive_listener_injection.h>
#include <grpcpp/passive_listener.h>

#include "src/core/lib/surface/server.h"

namespace grpc {
namespace experimental {
// A PIMPL wrapper class that owns the only ref to the passive listener
// implementation. This is returned to the application.
class PassiveListenerOwner final : public PassiveListener {
 public:
  explicit PassiveListenerOwner(std::shared_ptr<PassiveListener> listener)
      : listener_(std::move(listener)) {}
  void AcceptConnectedEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          endpoint) override {
    listener_->AcceptConnectedEndpoint(std::move(endpoint));
  }

  absl::Status AcceptConnectedFd(int fd) override {
    return listener_->AcceptConnectedFd(fd);
  }

  void Initialize(grpc_core::Server* /* server */,
                  grpc_core::ListenerInterface* /* listener */) override {}

 private:
  std::shared_ptr<PassiveListener> listener_;
};

// An implementation of the public C++ passive listener interface.
// The server builder holds a weak_ptr to one of these objects, and the
// application owns the instance.
class PassiveListenerImpl final : public PassiveListener {
 public:
  ~PassiveListenerImpl() override { GPR_ASSERT(server_.get() != nullptr); }
  void AcceptConnectedEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          endpoint) override;

  absl::Status AcceptConnectedFd(GRPC_UNUSED int fd) override;

  void Initialize(grpc_core::Server* server,
                  grpc_core::ListenerInterface* listener) override;

 private:
  // Data members will be populated when initialized.
  grpc_core::RefCountedPtr<grpc_core::Server> server_;
  // Not safe for this class to use directly -- only used within
  // grpc_server_accept_connected_endpoint().
  grpc_core::ListenerInterface* listener_ = nullptr;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPC_SRC_CPP_SERVER_PASSIVE_LISTENER_INTERNAL_H
