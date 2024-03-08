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

#include <grpc/grpc.h>
#include <grpcpp/passive_listener.h>
#include <grpcpp/server.h>

#include "src/core/lib/channel/channel_args.h"

namespace grpc {
namespace experimental {

// A PIMPL wrapper class that owns the only ref to the passive listener
// implementation. This is returned to the application.
class PassiveListenerOwner final : public grpc::experimental::PassiveListener {
 public:
  explicit PassiveListenerOwner(std::shared_ptr<PassiveListener> listener)
      : listener_(listener) {}
  void AcceptConnectedEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          endpoint) override {
    listener_->AcceptConnectedEndpoint(std::move(endpoint));
  }

  absl::Status AcceptConnectedFd(int fd) override {
    return listener_->AcceptConnectedFd(fd);
  }

 private:
  std::shared_ptr<PassiveListener> listener_;
};

// An implementation of the public C++ passive listener interface.
// The server builder holds a weak_ptr to one of these objects, and the
// application owns the instance.
class ServerBuilderPassiveListener final
    : public grpc::experimental::PassiveListener {
 public:
  explicit ServerBuilderPassiveListener(
      std::shared_ptr<grpc::ServerCredentials> creds)
      : creds_(std::move(creds)) {}

  ~ServerBuilderPassiveListener() override {
    grpc_channel_args_destroy(server_args_);
  }

  void AcceptConnectedEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          endpoint) override;

  absl::Status AcceptConnectedFd(GRPC_UNUSED int fd) override;

  void Initialize(grpc::Server* server, grpc::ChannelArguments& arguments);

 private:
  grpc::Server* server_ = nullptr;
  grpc_channel_args* server_args_ = nullptr;
  std::shared_ptr<grpc::ServerCredentials> creds_;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPC_SRC_CPP_SERVER_PASSIVE_LISTENER_INTERNAL_H
