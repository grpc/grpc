// Copyright 2026 gRPC authors.
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

#include "test/cpp/qps/session_util.h"

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/impl/generic_stub_session.h>
#include <grpcpp/impl/rpc_method.h>
#include <grpcpp/impl/server_callback_handlers.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "absl/synchronization/notification.h"

namespace grpc {
namespace testing {

namespace {

constexpr char kConnectSessionMethod[] =
    "/grpc.testing.BenchmarkService/ConnectSession";

}  // namespace

struct SessionState {
  absl::Notification ready;
  absl::Notification done;
  std::atomic<bool> ready_notified{false};
  std::mutex mu;
  std::shared_ptr<Channel> virtual_channel;
  grpc::Status status;
};

class SessionReactor : public grpc::experimental::ServerSessionReactor {
 public:
  SessionReactor() { StartVirtualRPCs(); }

  void OnSendInitialMetadataDone(bool /*ok*/) override {}

  void OnCancel() override {
    if (!finished_.test_and_set(std::memory_order_acq_rel)) {
      Finish(grpc::Status::CANCELLED);
    }
  }

  void OnDone() override { delete this; }

 private:
  std::atomic_flag finished_ = ATOMIC_FLAG_INIT;
};

OuterSessionService::OuterSessionService(grpc::Service* inner_service) {
  auto* method = new grpc::internal::RpcServiceMethod(
      kConnectSessionMethod, grpc::internal::RpcMethod::SESSION_RPC,
      new grpc::experimental::internal::CallbackSessionHandler<
          grpc::testing::SimpleRequest>(
          [](grpc::CallbackServerContext* /*context*/,
             const grpc::testing::SimpleRequest* /*request*/) {
            return new SessionReactor();
          },
          inner_service));
  method->SetServerApiType(
      grpc::internal::RpcServiceMethod::ApiType::CALL_BACK);
  AddMethod(method);
}

SessionHolder::SessionHolder(std::shared_ptr<Channel> virtual_channel,
                             std::unique_ptr<ClientContext> context,
                             std::shared_ptr<SessionState> state)
    : virtual_channel_(std::move(virtual_channel)),
      context_(std::move(context)),
      state_(std::move(state)) {}

SessionHolder::~SessionHolder() { Close(); }

void SessionHolder::Close() {
  if (context_) {
    context_->TryCancel();
    state_->done.WaitForNotification();
    context_.reset();
  }
}

class ClientSessionReactor : public grpc::experimental::ClientSessionReactor {
 public:
  explicit ClientSessionReactor(std::shared_ptr<SessionState> state)
      : state_(std::move(state)) {}

  void OnSessionReady(std::shared_ptr<Channel> virtual_channel) override {
    {
      std::lock_guard<std::mutex> l(state_->mu);
      state_->virtual_channel = std::move(virtual_channel);
    }
    if (!state_->ready_notified.exchange(true, std::memory_order_acq_rel)) {
      state_->ready.Notify();
    }
  }

  void OnDone(const grpc::Status& s) override {
    {
      std::lock_guard<std::mutex> l(state_->mu);
      state_->status = s;
    }
    if (!state_->ready_notified.exchange(true, std::memory_order_acq_rel)) {
      state_->ready.Notify();
    }
    state_->done.Notify();
    delete this;
  }

 private:
  std::shared_ptr<SessionState> state_;
};

std::unique_ptr<SessionHolder> EstablishSession(
    std::shared_ptr<Channel> channel) {
  auto stub = std::make_unique<grpc::experimental::GenericStubSession<
      grpc::testing::SimpleRequest, grpc::testing::SimpleResponse>>(channel);
  grpc::testing::SimpleRequest request;
  auto context = std::make_unique<ClientContext>();
  auto state = std::make_shared<SessionState>();

  auto* session_reactor = new ClientSessionReactor(state);

  stub->PrepareSessionCall(context.get(), kConnectSessionMethod, {}, &request,
                           session_reactor);
  session_reactor->StartCall();

  state->ready.WaitForNotification();

  std::shared_ptr<Channel> virtual_channel;
  {
    std::lock_guard<std::mutex> l(state->mu);
    virtual_channel = state->virtual_channel;
  }

  if (virtual_channel == nullptr) {
    state->done.WaitForNotification();
    return nullptr;
  }

  return std::make_unique<SessionHolder>(std::move(virtual_channel),
                                         std::move(context), std::move(state));
}

}  // namespace testing
}  // namespace grpc
