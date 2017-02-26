/*
 *
 * Copyright 2015, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc++/resource_quota.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>

#include "src/proto/grpc/testing/services.grpc.pb.h"
#include "test/cpp/qps/server.h"
#include "test/cpp/qps/usage_timer.h"

namespace grpc {
namespace testing {

class BenchmarkServiceImpl final : public BenchmarkService::Service {
 public:
  Status UnaryCall(ServerContext* context, const SimpleRequest* request,
                   SimpleResponse* response) override {
    if (request->response_size() > 0) {
      if (!Server::SetPayload(request->response_type(),
                              request->response_size(),
                              response->mutable_payload())) {
        return Status(grpc::StatusCode::INTERNAL, "Error creating payload.");
      }
    }
    return Status::OK;
  }
  Status StreamingCall(
      ServerContext* context,
      ServerReaderWriter<SimpleResponse, SimpleRequest>* stream) override {
    SimpleRequest request;
    while (stream->Read(&request)) {
      SimpleResponse response;
      if (request.response_size() > 0) {
        if (!Server::SetPayload(request.response_type(),
                                request.response_size(),
                                response.mutable_payload())) {
          return Status(grpc::StatusCode::INTERNAL, "Error creating payload.");
        }
      }
      stream->Write(response);
    }
    return Status::OK;
  }
};

class SynchronousServer final : public grpc::testing::Server {
 public:
  explicit SynchronousServer(const ServerConfig& config) : Server(config) {
    ServerBuilder builder;

    char* server_address = NULL;

    gpr_join_host_port(&server_address, "::", port());
    builder.AddListeningPort(server_address,
                             Server::CreateServerCredentials(config));
    gpr_free(server_address);

    if (config.resource_quota_size() > 0) {
      builder.SetResourceQuota(ResourceQuota("AsyncQpsServerTest")
                                   .Resize(config.resource_quota_size()));
    }

    builder.RegisterService(&service_);

    impl_ = builder.BuildAndStart();
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
