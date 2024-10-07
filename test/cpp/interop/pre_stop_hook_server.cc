//
//
// Copyright 2023 gRPC authors.
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
//
//

#include "test/cpp/interop/pre_stop_hook_server.h"

#include <grpcpp/grpcpp.h>

#include <thread>

#include "absl/strings/str_format.h"
#include "src/core/util/sync.h"
#include "src/proto/grpc/testing/messages.pb.h"

namespace grpc {
namespace testing {
namespace {

enum class State : std::uint8_t { kNew, kWaiting, kDone, kShuttingDown };

std::unique_ptr<Server> BuildHookServer(HookServiceImpl* service, int port) {
  ServerBuilder builder;
  builder.AddListeningPort(absl::StrFormat("0.0.0.0:%d", port),
                           grpc::InsecureServerCredentials());
  builder.RegisterService(service);
  return builder.BuildAndStart();
}

}  // namespace

class PreStopHookServer {
 public:
  explicit PreStopHookServer(int port, const absl::Duration& startup_timeout)
      : server_(BuildHookServer(&hook_service_, port)),
        server_thread_(PreStopHookServer::ServerThread, this) {
    WaitForState(State::kWaiting, startup_timeout);
  }

  ~PreStopHookServer() {
    hook_service_.Stop();
    SetState(State::kShuttingDown);
    server_->Shutdown();
    WaitForState(State::kDone, absl::Seconds(5));
    server_thread_.detach();
  }

  State GetState() {
    grpc_core::MutexLock lock(&mu_);
    return state_;
  }

  void SetState(State state) {
    grpc_core::MutexLock lock(&mu_);
    state_ = state;
    condition_.SignalAll();
  }

  void SetReturnStatus(const Status& status) {
    hook_service_.AddReturnStatus(status);
  }

  bool TestOnlyExpectRequests(size_t expected_requests_count,
                              absl::Duration timeout) {
    return hook_service_.TestOnlyExpectRequests(expected_requests_count,
                                                timeout);
  }

 private:
  bool WaitForState(State state, const absl::Duration& timeout) {
    grpc_core::MutexLock lock(&mu_);
    auto deadline = absl::Now() + timeout;
    while (state_ != state && !condition_.WaitWithDeadline(&mu_, deadline)) {
    }
    return state_ == state;
  }

  static void ServerThread(PreStopHookServer* server) {
    server->SetState(State::kWaiting);
    server->server_->Wait();
    server->SetState(State::kDone);
  }

  HookServiceImpl hook_service_;
  grpc_core::Mutex mu_;
  grpc_core::CondVar condition_ ABSL_GUARDED_BY(mu_);
  State state_ ABSL_GUARDED_BY(mu_) = State::kNew;
  std::unique_ptr<Server> server_;
  std::thread server_thread_;
};

Status PreStopHookServerManager::Start(int port, size_t timeout_s) {
  if (server_) {
    return Status(StatusCode::ALREADY_EXISTS,
                  "Pre hook server is already running");
  }
  server_ = std::unique_ptr<PreStopHookServer, PreStopHookServerDeleter>(
      new PreStopHookServer(port, absl::Seconds(timeout_s)),
      PreStopHookServerDeleter());
  return server_->GetState() == State::kWaiting
             ? Status::OK
             : Status(StatusCode::DEADLINE_EXCEEDED, "Server have not started");
}

Status PreStopHookServerManager::Stop() {
  if (!server_) {
    return Status(StatusCode::UNAVAILABLE, "Pre hook server is not running");
  }
  server_.reset();
  return Status::OK;
}

void PreStopHookServerManager::Return(StatusCode code,
                                      absl::string_view description) {
  server_->SetReturnStatus(Status(code, std::string(description)));
}

bool PreStopHookServerManager::TestOnlyExpectRequests(
    size_t expected_requests_count, const absl::Duration& timeout) {
  return server_->TestOnlyExpectRequests(expected_requests_count, timeout);
}

void PreStopHookServerManager::PreStopHookServerDeleter::operator()(
    PreStopHookServer* server) {
  delete server;
}

//
// HookServiceImpl
//

ServerUnaryReactor* HookServiceImpl::Hook(CallbackServerContext* context,
                                          const Empty* /* request */,
                                          Empty* /* reply */) {
  auto reactor = context->DefaultReactor();
  grpc_core::MutexLock lock(&mu_);
  pending_requests_.emplace_back(reactor);
  MatchRequestsAndStatuses();
  return reactor;
}

ServerUnaryReactor* HookServiceImpl::SetReturnStatus(
    CallbackServerContext* context, const SetReturnStatusRequest* request,
    Empty* /* reply */) {
  auto reactor = context->DefaultReactor();
  reactor->Finish(Status::OK);
  grpc_core::MutexLock lock(&mu_);
  respond_all_status_.emplace(
      static_cast<StatusCode>(request->grpc_code_to_return()),
      request->grpc_status_description());
  MatchRequestsAndStatuses();
  return reactor;
}

ServerUnaryReactor* HookServiceImpl::ClearReturnStatus(
    CallbackServerContext* context, const Empty* /* request */,
    Empty* /* reply */) {
  auto reactor = context->DefaultReactor();
  reactor->Finish(Status::OK);
  grpc_core::MutexLock lock(&mu_);
  respond_all_status_.reset();
  MatchRequestsAndStatuses();
  return reactor;
}

void HookServiceImpl::AddReturnStatus(const Status& status) {
  grpc_core::MutexLock lock(&mu_);
  pending_statuses_.push_back(status);
  MatchRequestsAndStatuses();
}

bool HookServiceImpl::TestOnlyExpectRequests(size_t expected_requests_count,
                                             const absl::Duration& timeout) {
  grpc_core::MutexLock lock(&mu_);
  auto deadline = absl::Now() + timeout;
  while (pending_requests_.size() < expected_requests_count &&
         !request_var_.WaitWithDeadline(&mu_, deadline)) {
  }
  return pending_requests_.size() >= expected_requests_count;
}

void HookServiceImpl::Stop() {
  grpc_core::MutexLock lock(&mu_);
  if (!respond_all_status_.has_value()) {
    respond_all_status_.emplace(StatusCode::ABORTED, "Shutting down");
  }
  MatchRequestsAndStatuses();
}

void HookServiceImpl::MatchRequestsAndStatuses() {
  while (!pending_requests_.empty() && !pending_statuses_.empty()) {
    pending_requests_.front()->Finish(std::move(pending_statuses_.front()));
    pending_requests_.erase(pending_requests_.begin());
    pending_statuses_.erase(pending_statuses_.begin());
  }
  if (respond_all_status_.has_value()) {
    for (const auto& request : pending_requests_) {
      request->Finish(*respond_all_status_);
    }
    pending_requests_.clear();
  }
  request_var_.SignalAll();
}

}  // namespace testing
}  // namespace grpc
