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

#include <atomic>
#include <thread>

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
    auto s = SetResponse(request, response);
    if (!s.ok()) {
      return s;
    }
    return Status::OK;
  }
  Status StreamingCall(
      ServerContext* context,
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
    SimpleResponse dummy;
    auto cp = ClientPull(context, stream, &dummy);
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
  static Status ClientPull(ServerContext* context,
                           ReaderInterface<SimpleRequest>* stream,
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
  static Status ServerPush(ServerContext* context,
                           WriterInterface<SimpleResponse>* stream,
                           const SimpleResponse& response,
                           std::function<bool()> done) {
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
    ServerBuilder builder;

    char* server_address = NULL;

    gpr_join_host_port(&server_address, "::", port());
    builder.AddListeningPort(server_address,
                             Server::CreateServerCredentials(config));
    gpr_free(server_address);

    ApplyServerConfig(config, &builder);

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
