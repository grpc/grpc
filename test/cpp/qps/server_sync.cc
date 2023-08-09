//
//
// Copyright 2015 gRPC authors.
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

#include <atomic>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_context.h>

#include "src/core/lib/gprpp/host_port.h"
#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "test/cpp/qps/qps_server_builder.h"
#include "test/cpp/qps/server.h"
#include "test/cpp/qps/usage_timer.h"

namespace grpc {
namespace testing {

class BenchmarkServiceImpl final : public BenchmarkService::Service {
 public:
  Status UnaryCall(ServerContext* /*context*/, const SimpleRequest* request,
                   SimpleResponse* response) override {
    auto s = SetResponse(request, response);
    if (!s.ok()) {
      return s;
    }
    return Status::OK;
  }
  Status StreamingCall(
      ServerContext* /*context*/,
      ServerReaderWriter<SimpleResponse, SimpleRequest>* stream) override {
    SimpleRequest request;
    while (stream->Read(&request)) {
      SimpleResponse response;
      auto s = SetResponse(&request, &response);
      if (!s.ok()) {
        return s;
      }
      if (!stream->Write(response)) {
        return Status(StatusCode::INTERNAL, "Server couldn't respond");
      }
    }
    return Status::OK;
  }
  Status StreamingFromClient(ServerContext* context,
                             ServerReader<SimpleRequest>* stream,
                             SimpleResponse* response) override {
    auto s = ClientPull(context, stream, response);
    if (!s.ok()) {
      return s;
    }
    return Status::OK;
  }
  Status StreamingFromServer(ServerContext* context,
                             const SimpleRequest* request,
                             ServerWriter<SimpleResponse>* stream) override {
    SimpleResponse response;
    auto s = SetResponse(request, &response);
    if (!s.ok()) {
      return s;
    }
    return ServerPush(context, stream, response, nullptr);
  }
  Status StreamingBothWays(
      ServerContext* context,
      ServerReaderWriter<SimpleResponse, SimpleRequest>* stream) override {
    // Read the first client message to setup server response
    SimpleRequest request;
    if (!stream->Read(&request)) {
      return Status::OK;
    }
    SimpleResponse response;
    auto s = SetResponse(&request, &response);
    if (!s.ok()) {
      return s;
    }
    std::atomic_bool done;
    Status sp;
    std::thread t([context, stream, &response, &done, &sp]() {
      sp = ServerPush(context, stream, response, [&done]() {
        return done.load(std::memory_order_relaxed);
      });
    });
    SimpleResponse phony;
    auto cp = ClientPull(context, stream, &phony);
    done.store(true, std::memory_order_relaxed);  // can be lazy
    t.join();
    if (!cp.ok()) {
      return cp;
    }
    if (!sp.ok()) {
      return sp;
    }
    return Status::OK;
  }

 private:
  template <class R>
  static Status ClientPull(ServerContext* /*context*/, R* stream,
                           SimpleResponse* response) {
    SimpleRequest request;
    while (stream->Read(&request)) {
    }
    if (request.response_size() > 0) {
      if (!Server::SetPayload(request.response_type(), request.response_size(),
                              response->mutable_payload())) {
        return Status(grpc::StatusCode::INTERNAL, "Error creating payload.");
      }
    }
    return Status::OK;
  }
  template <class W>
  static Status ServerPush(ServerContext* /*context*/, W* stream,
                           const SimpleResponse& response,
                           const std::function<bool()>& done) {
    while ((done == nullptr) || !done()) {
      // TODO(vjpai): Add potential for rate-pacing on this
      if (!stream->Write(response)) {
        return Status(StatusCode::INTERNAL, "Server couldn't push");
      }
    }
    return Status::OK;
  }
  static Status SetResponse(const SimpleRequest* request,
                            SimpleResponse* response) {
    if (request->response_size() > 0) {
      if (!Server::SetPayload(request->response_type(),
                              request->response_size(),
                              response->mutable_payload())) {
        return Status(grpc::StatusCode::INTERNAL, "Error creating payload.");
      }
    }
    return Status::OK;
  }
};

class SynchronousServer final : public grpc::testing::Server {
 public:
  explicit SynchronousServer(const ServerConfig& config) : Server(config) {
    std::unique_ptr<ServerBuilder> builder = CreateQpsServerBuilder();

    auto port_num = port();
    // Negative port number means inproc server, so no listen port needed
    if (port_num >= 0) {
      std::string server_address = grpc_core::JoinHostPort("::", port_num);
      builder->AddListeningPort(
          server_address, Server::CreateServerCredentials(config), &port_num);
    }

    ApplyConfigToBuilder(config, builder.get());

    builder->RegisterService(&service_);

    impl_ = builder->BuildAndStart();
    if (impl_ == nullptr) {
      gpr_log(GPR_ERROR, "Server: Fail to BuildAndStart(port=%d)", port_num);
    } else {
      gpr_log(GPR_INFO, "Server: BuildAndStart(port=%d)", port_num);
    }
  }

  std::shared_ptr<Channel> InProcessChannel(
      const ChannelArguments& args) override {
    return impl_->InProcessChannel(args);
  }

 private:
  BenchmarkServiceImpl service_;
  std::unique_ptr<grpc::Server> impl_;
};

std::unique_ptr<grpc::testing::Server> CreateSynchronousServer(
    const ServerConfig& config) {
  return std::unique_ptr<Server>(new SynchronousServer(config));
}

}  // namespace testing
}  // namespace grpc
