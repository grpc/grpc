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
#ifndef GRPC_SRC_CORE_LIB_SURFACE_PASSIVE_LISTENER_INTERNAL_H
#define GRPC_SRC_CORE_LIB_SURFACE_PASSIVE_LISTENER_INTERNAL_H

#include <grpc/support/port_platform.h>

#include <grpc/passive_listener.h>

#include "src/core/lib/surface/server.h"

namespace grpc_core {
namespace experimental {

// An implementation of the public C++ passive listener interface.
// The server builder holds a weak_ptr to one of these objects, and the
// application owns the instance.
class PassiveListenerImpl final : public PassiveListener {
 public:
  absl::Status AcceptConnectedEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          endpoint) override;

  absl::Status AcceptConnectedFd(GRPC_UNUSED int fd) override;

 private:
  // note: the grpc_core::Server redundant namespace qualification is required
  // for older gcc versions.
  friend absl::Status(::grpc_server_add_passive_listener)(
      grpc_core::Server* server, grpc_server_credentials* credentials,
      PassiveListenerImpl& passive_listener);

  // Data members will be populated when initialized.
  Server* server_ = nullptr;
  RefCountedPtr<Server::ListenerInterface> listener_;
};

}  // namespace experimental
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SURFACE_PASSIVE_LISTENER_INTERNAL_H
