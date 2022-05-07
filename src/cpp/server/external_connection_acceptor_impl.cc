/*
 *
 * Copyright 2019 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/cpp/server/external_connection_acceptor_impl.h"

#include <memory>
#include <utility>

#include <grpc/support/log.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/byte_buffer.h>
#include <grpcpp/support/channel_arguments.h>

namespace grpc {
namespace internal {
namespace {
// The actual type to return to user. It co-owns the internal impl object with
// the server.
class AcceptorWrapper : public experimental::ExternalConnectionAcceptor {
 public:
  explicit AcceptorWrapper(std::shared_ptr<ExternalConnectionAcceptorImpl> impl)
      : impl_(std::move(impl)) {}
  void HandleNewConnection(NewConnectionParameters* p) override {
    impl_->HandleNewConnection(p);
  }

 private:
  std::shared_ptr<ExternalConnectionAcceptorImpl> impl_;
};
}  // namespace

ExternalConnectionAcceptorImpl::ExternalConnectionAcceptorImpl(
    const std::string& name,
    ServerBuilder::experimental_type::ExternalConnectionType type,
    std::shared_ptr<ServerCredentials> creds)
    : name_(name), creds_(std::move(creds)) {
  GPR_ASSERT(type ==
             ServerBuilder::experimental_type::ExternalConnectionType::FROM_FD);
}

std::unique_ptr<experimental::ExternalConnectionAcceptor>
ExternalConnectionAcceptorImpl::GetAcceptor() {
  grpc_core::MutexLock lock(&mu_);
  GPR_ASSERT(!has_acceptor_);
  has_acceptor_ = true;
  return std::unique_ptr<experimental::ExternalConnectionAcceptor>(
      new AcceptorWrapper(shared_from_this()));
}

void ExternalConnectionAcceptorImpl::HandleNewConnection(
    experimental::ExternalConnectionAcceptor::NewConnectionParameters* p) {
  grpc_core::MutexLock lock(&mu_);
  if (shutdown_ || !started_) {
    // TODO(yangg) clean up.
    gpr_log(
        GPR_ERROR,
        "NOT handling external connection with fd %d, started %d, shutdown %d",
        p->fd, started_, shutdown_);
    return;
  }
  if (handler_) {
    handler_->Handle(p->listener_fd, p->fd, p->read_buffer.c_buffer());
  }
}

void ExternalConnectionAcceptorImpl::Shutdown() {
  grpc_core::MutexLock lock(&mu_);
  shutdown_ = true;
}

void ExternalConnectionAcceptorImpl::Start() {
  grpc_core::MutexLock lock(&mu_);
  GPR_ASSERT(!started_);
  GPR_ASSERT(has_acceptor_);
  GPR_ASSERT(!shutdown_);
  started_ = true;
}

void ExternalConnectionAcceptorImpl::SetToChannelArgs(ChannelArguments* args) {
  args->SetPointer(name_.c_str(), &handler_);
}

}  // namespace internal
}  // namespace grpc
