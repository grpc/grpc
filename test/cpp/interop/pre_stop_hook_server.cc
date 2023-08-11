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

#include <thread>

#include "absl/strings/str_format.h"

#include <grpcpp/grpcpp.h>

#include "src/proto/grpc/testing/test.grpc.pb.h"

namespace grpc {
namespace testing {

namespace {

class HookServiceImpl final : public HookService::CallbackService {
 public:
  ServerUnaryReactor* Hook(CallbackServerContext* context,
                           const Empty* /* request */,
                           Empty* /* reply */) override {
    grpc_core::MutexLock lock(&mu_);
    auto reactor = context->DefaultReactor();
    if (pending_status_) {
      reactor->Finish(std::move(*pending_status_));
      pending_status_ = absl::nullopt;
    } else {
      pending_requests_.push_back(reactor);
    }
    request_var_.SignalAll();
    return reactor;
  }

  void SetReturnStatus(const Status& status) {
    grpc_core::MutexLock lock(&mu_);
    if (pending_requests_.empty()) {
      pending_status_ = status;
    }
    for (auto request : pending_requests_) {
      request->Finish(status);
    }
    pending_requests_.clear();
    request_var_.SignalAll();
  }

  bool ExpectRequests(size_t expected_requests_count, size_t timeout_s) {
    grpc_core::MutexLock lock(&mu_);
    auto deadline = absl::Now() + absl::Seconds(timeout_s);
    while (pending_requests_.size() < expected_requests_count &&
           !request_var_.WaitWithDeadline(&mu_, deadline)) {
    }
    return pending_requests_.size() >= expected_requests_count;
  }

 private:
  grpc_core::Mutex mu_;
  grpc_core::CondVar request_var_ ABSL_GUARDED_BY(&mu_);
  absl::optional<Status> pending_status_ ABSL_GUARDED_BY(&mu_);
  std::vector<ServerUnaryReactor*> pending_requests_ ABSL_GUARDED_BY(&mu_);
};

class ServerHolder {
 public:
  enum class State { kNew, kWaiting, kDone, kShuttingDown };

  explicit ServerHolder(int port) {
    ServerBuilder builder;
    builder.AddListeningPort(absl::StrFormat("0.0.0.0:%d", port),
                             grpc::InsecureServerCredentials());
    builder.RegisterService(&hook_service_);
    server_ = builder.BuildAndStart();
  }

  bool WaitForState(State state, absl::Duration timeout) {
    auto deadline = absl::Now() + timeout;
    grpc_core::MutexLock lock(&mu_);
    while (state_ != state && !condition_.WaitWithDeadline(&mu_, deadline)) {
    }
    return state_ == state;
  }

  void Shutdown() { server_->Shutdown(); }

  void SetReturnStatus(Status status) { hook_service_.SetReturnStatus(status); }

  State state() {
    grpc_core::MutexLock lock(&mu_);
    return state_;
  }

  bool ExpectRequests(size_t expected_requests_count, size_t timeout_s) {
    return hook_service_.ExpectRequests(expected_requests_count, timeout_s);
  }

  static void RunServer(std::shared_ptr<ServerHolder> server) {
    {
      grpc_core::MutexLock lock(&server->mu_);
      server->state_ = State::kWaiting;
      server->condition_.SignalAll();
    }
    server->server_->Wait();
    {
      grpc_core::MutexLock lock(&server->mu_);
      server->state_ = State::kShuttingDown;
      server->condition_.SignalAll();
    }
  }

 private:
  grpc_core::Mutex mu_;
  grpc_core::CondVar condition_ ABSL_GUARDED_BY(mu_);
  State state_ ABSL_GUARDED_BY(mu_) = State::kNew;
  std::unique_ptr<Server> server_;
  HookServiceImpl hook_service_;
};
}  // namespace

class PreStopHookServer {
 public:
  explicit PreStopHookServer(int port, int timeout_s = 15)
      : server_(std::make_shared<ServerHolder>(port)),
        thread_(ServerHolder::RunServer, server_) {
    server_->WaitForState(ServerHolder::State::kWaiting,
                          absl::Seconds(timeout_s));
  }

  ~PreStopHookServer() {
    server_->Shutdown();
    thread_.join();
  }

  Status startup_status() const {
    return server_->state() == ServerHolder::State::kWaiting
               ? Status::OK
               : Status(StatusCode::DEADLINE_EXCEEDED,
                        "Server have not started");
  }

  void SetReturnStatus(Status status) { server_->SetReturnStatus(status); }

  bool ExpectRequests(size_t expected_requests_count, size_t timeout_s) {
    return server_->ExpectRequests(expected_requests_count, timeout_s);
  }

 private:
  std::shared_ptr<ServerHolder> server_;
  std::thread thread_;
};

Status PreStopHookServerManager::Start(int port) {
  if (server_) {
    return Status(StatusCode::ALREADY_EXISTS,
                  "Pre hook server is already running");
  }
  server_ = std::unique_ptr<PreStopHookServer,
                            PreStopHookServerManager::DeleteServer>(
      new PreStopHookServer(port), PreStopHookServerManager::DeleteServer());
  return server_->startup_status();
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

bool PreStopHookServerManager::ExpectRequests(size_t expected_requests_count,
                                              size_t timeout_s) {
  return server_->ExpectRequests(expected_requests_count, timeout_s);
}

void PreStopHookServerManager::DeleteServer::operator()(
    PreStopHookServer* server) {
  delete server;
}

}  // namespace testing
}  // namespace grpc
