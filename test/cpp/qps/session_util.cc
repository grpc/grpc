// Copyright 2024 gRPC authors.
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

#include <grpcpp/impl/generic_stub_session.h>

#include "grpcpp/impl/server_callback_handlers.h"
#include "grpcpp/virtual_channel.h"

#include <atomic>

namespace grpc {
namespace testing {

class SessionReactor : public grpc::experimental::ServerSessionReactor {
 public:
  explicit SessionReactor() {
    StartVirtualRPCs();
    // Keep the session open for 1 hour to allow benchmark calls to succeed.
    alarm_.Set(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                            gpr_time_from_seconds(3600, GPR_TIMESPAN)),
               [this](bool ok) {
                 if (ok) {
                   MaybeFinish(Status::OK);
                 }
                 Unref();
               });
  }

  void OnSendInitialMetadataDone(bool /*ok*/) override {}

  void OnCancel() override {
    alarm_.Cancel();
    MaybeFinish(grpc::Status::CANCELLED);
  }

  void OnDone() override { Unref(); }

 private:
  void MaybeFinish(Status s) {
    if (!finish_called_.exchange(true)) {
      Finish(std::move(s));
    }
  }

  void Unref() {
    if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

  grpc::Alarm alarm_;
  std::atomic<int> refs_{2};  // 1 for gRPC, 1 for Alarm
  std::atomic<bool> finish_called_{false};
};

OuterSessionService::OuterSessionService(grpc::Service* inner_service) {
  auto* method = new grpc::internal::RpcServiceMethod(
      "/grpc.testing.BenchmarkService/ConnectSession",
      grpc::internal::RpcMethod::SESSION_RPC,
      new grpc::experimental::internal::CallbackSessionHandler<grpc::testing::SimpleRequest>(
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
                             std::shared_ptr<absl::Notification> done)
    : virtual_channel_(std::move(virtual_channel)),
      context_(std::move(context)),
      done_(std::move(done)) {}

SessionHolder::~SessionHolder() { Close(); }

void SessionHolder::Close() {
  if (context_) {
    context_->TryCancel();
    done_->WaitForNotification();
    context_.reset();
  }
}

class ClientSessionReactor : public grpc::experimental::ClientSessionReactor {
 public:
  ClientSessionReactor(std::function<void(grpc::internal::Call)> on_ready,
                       std::function<void(const grpc::Status&)> on_done)
      : on_ready_(std::move(on_ready)), on_done_(std::move(on_done)) {}

  void OnSessionReady(grpc::internal::Call call) override {
    if (on_ready_) on_ready_(std::move(call));
  }

  void OnDone(const grpc::Status& s) override {
    if (on_done_) on_done_(s);
    delete this;
  }

 private:
  std::function<void(grpc::internal::Call)> on_ready_;
  std::function<void(const grpc::Status&)> on_done_;
};

std::unique_ptr<SessionHolder> EstablishSession(
    std::shared_ptr<Channel> channel) {
  auto stub = std::make_unique<grpc::experimental::GenericStubSession<
      grpc::testing::SimpleRequest, grpc::testing::SimpleResponse>>(channel);
  grpc::testing::SimpleRequest request;
  auto context = std::make_unique<ClientContext>();
  auto done = std::make_shared<absl::Notification>();
  auto ready = std::make_shared<absl::Notification>();
  auto ready_notified = std::make_shared<std::atomic<bool>>(false);
  std::shared_ptr<Channel> virtual_channel;

  auto* session_reactor = new ClientSessionReactor(
      [&virtual_channel, ready, ready_notified](grpc::internal::Call call) {
        virtual_channel =
            grpc::experimental::CreateVirtualChannel(std::move(call));
        if (!ready_notified->exchange(true)) {
          ready->Notify();
        }
      },
      [done, ready, ready_notified](const grpc::Status& s) {
        if (!s.ok()) {
          if (!ready_notified->exchange(true)) {
            ready->Notify();
          }
        }
        done->Notify();
      });

  stub->PrepareSessionCall(context.get(),
                           "/grpc.testing.BenchmarkService/ConnectSession", {},
                           &request, session_reactor);
  session_reactor->StartCall();

  ready->WaitForNotification();

  if (virtual_channel == nullptr) {
    return nullptr;
  }

  return std::make_unique<SessionHolder>(std::move(virtual_channel),
                                         std::move(context), std::move(done));
}

}  // namespace testing
}  // namespace grpc
