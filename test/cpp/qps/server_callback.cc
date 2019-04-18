/*
 *
 * Copyright 2015 gRPC authors.
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
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_context.h>

#include "src/core/lib/gpr/host_port.h"
#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "test/cpp/qps/qps_server_builder.h"
#include "test/cpp/qps/server.h"
#include "test/cpp/qps/usage_timer.h"

namespace grpc {
namespace testing {

class BenchmarkCallbackServiceImpl final
    : public BenchmarkService::ExperimentalCallbackService {
 public:
  void UnaryCall(
      ServerContext* context, const ::grpc::testing::SimpleRequest* request,
      ::grpc::testing::SimpleResponse* response,
      ::grpc::experimental::ServerCallbackRpcController* controller) override {
    auto s = SetResponse(request, response);
    controller->Finish(s);
  }

  ::grpc::experimental::ServerBidiReactor<::grpc::testing::SimpleRequest,
                                          ::grpc::testing::SimpleResponse>*
  StreamingCall() override {
    class Reactor
        : public ::grpc::experimental::ServerBidiReactor<
              ::grpc::testing::SimpleRequest, ::grpc::testing::SimpleResponse> {
     public:
      Reactor() {}
      void OnStarted(ServerContext* context) override { StartRead(&request_); }

      void OnReadDone(bool ok) override {
        if (!ok) {
          Finish(::grpc::Status::OK);
          return;
        }
        auto s = SetResponse(&request_, &response_);
        if (!s.ok()) {
          Finish(s);
          return;
        }
        StartWrite(&response_);
      }

      void OnWriteDone(bool ok) override {
        if (!ok) {
          Finish(::grpc::Status::OK);
          return;
        }
        StartRead(&request_);
      }

      void OnDone() override { delete (this); }

     private:
      SimpleRequest request_;
      SimpleResponse response_;
    };
    return new Reactor;
  }

 private:
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

class CallbackServer final : public grpc::testing::Server {
 public:
  explicit CallbackServer(const ServerConfig& config) : Server(config) {
    std::unique_ptr<ServerBuilder> builder = CreateQpsServerBuilder();

    auto port_num = port();
    // Negative port number means inproc server, so no listen port needed
    if (port_num >= 0) {
      char* server_address = nullptr;
      gpr_join_host_port(&server_address, "::", port_num);
      builder->AddListeningPort(server_address,
                                Server::CreateServerCredentials(config));
      gpr_free(server_address);
    }

    ApplyConfigToBuilder(config, builder.get());

    builder->RegisterService(&service_);

    impl_ = builder->BuildAndStart();
  }

  std::shared_ptr<Channel> InProcessChannel(
      const ChannelArguments& args) override {
    return impl_->InProcessChannel(args);
  }

 private:
  BenchmarkCallbackServiceImpl service_;
  std::unique_ptr<grpc::Server> impl_;
};

std::unique_ptr<grpc::testing::Server> CreateCallbackServer(
    const ServerConfig& config) {
  return std::unique_ptr<Server>(new CallbackServer(config));
}

}  // namespace testing
}  // namespace grpc
