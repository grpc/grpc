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

#include <memory>
#include <iostream>
#include <string>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc++/async_unary_call.h>
#include <grpc++/completion_queue.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <grpc++/status.h>
#include "helloworld.grpc.pb.h"

using grpc::CompletionQueue;
using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using helloworld::HelloRequest;
using helloworld::HelloReply;
using helloworld::Greeter;

static bool got_sigint = false;

class ServerImpl final {
 public:
  ServerImpl() : service_(&service_cq_) {}

  ~ServerImpl() {
    server_->Shutdown();
    rpc_cq_.Shutdown();
    service_cq_.Shutdown();
  }

  // There is no shutdown handling in this code.
  void Run() {
    std::string server_address("0.0.0.0:50051");

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterAsyncService(&service_);
    server_ = builder.BuildAndStart();
    std::cout << "Server listening on " << server_address << std::endl;

    while (true) {
      CallData* rpc = new CallData();
      service_.RequestSayHello(&rpc->ctx, &rpc->request, &rpc->responder,
                               &rpc_cq_, rpc);
      void* got_tag;
      bool ok;
      service_cq_.Next(&got_tag, &ok);
      GPR_ASSERT(ok);
      GPR_ASSERT(got_tag == rpc);

      std::thread t(&ServerImpl::HandleRpc, this, rpc);
      t.detach();
    }
  }

 private:
  struct CallData {
    CallData() : responder(&ctx) {}
    ServerContext ctx;
    HelloRequest request;
    HelloReply reply;
    ServerAsyncResponseWriter<HelloReply> responder;
  };

  // Runs in a detached thread, processes rpc then deletes data.
  void HandleRpc(CallData* rpc) {
    std::string prefix("Hello ");
    rpc->reply.set_message(prefix + rpc->request.name());
    rpc->responder.Finish(rpc->reply, Status::OK, &rpc->ctx);
    void* got_tag;
    bool ok;
    rpc_cq_.Next(&got_tag, &ok);
    GPR_ASSERT(ok);
    GPR_ASSERT(got_tag == &rpc->ctx);

    delete rpc;
  }

  CompletionQueue service_cq_;
  CompletionQueue rpc_cq_;
  Greeter::AsyncService service_;
  std::unique_ptr<Server> server_;
};

int main(int argc, char** argv) {
  grpc_init();

  ServerImpl server;
  server.Run();

  grpc_shutdown();
  return 0;
}
