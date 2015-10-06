/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <thread>

#include <gflags/gflags.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/security/server_credentials.h>

#include "test/cpp/qps/qpstest.grpc.pb.h"
#include "test/cpp/qps/server.h"
#include "test/cpp/qps/timer.h"

namespace grpc {
namespace testing {

class TestServiceImpl GRPC_FINAL : public TestService::Service {
 public:
  Status UnaryCall(ServerContext* context, const SimpleRequest* request,
                   SimpleResponse* response) GRPC_OVERRIDE {
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
      ServerReaderWriter<SimpleResponse, SimpleRequest>* stream) GRPC_OVERRIDE {
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

class SynchronousServer GRPC_FINAL : public grpc::testing::Server {
 public:
  SynchronousServer(const ServerConfig& config, int port)
      : impl_(MakeImpl(port)) {}

 private:
  std::unique_ptr<grpc::Server> MakeImpl(int port) {
    ServerBuilder builder;

    char* server_address = NULL;
    gpr_join_host_port(&server_address, "::", port);
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    gpr_free(server_address);

    builder.RegisterService(&service_);

    return builder.BuildAndStart();
  }

  TestServiceImpl service_;
  std::unique_ptr<grpc::Server> impl_;
};

std::unique_ptr<grpc::testing::Server> CreateSynchronousServer(
    const ServerConfig& config, int port) {
  return std::unique_ptr<Server>(new SynchronousServer(config, port));
}

}  // namespace testing
}  // namespace grpc
