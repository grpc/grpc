/*
 *
 * Copyright 2016, Google Inc.
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

#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <grpc++/grpc++.h>

#include "hellostreamingworld.grpc.pb.h"

using grpc::Server;
using grpc::ServerAsyncReaderWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;
using grpc::Status;
using hellostreamingworld::HelloRequest;
using hellostreamingworld::HelloReply;
using hellostreamingworld::MultiGreeter;

enum class Type { READ = 1, WRITE = 2, CONNECT = 3, DONE = 4, FINISH = 5 };

// NOTE: This is a complex example for an asynchronous, bidirectional streaming
// server. For a simpler example, start with the
// greeter_server/greeter_async_server first.

// Most of the logic is similar to AsyncBidiGreeterClient, so follow that class
// for detailed comments. Two main differences between the server and the client
// are: (a) Server cannot initiate a connection, so it first waits for a
// 'connection'. (b) Server can handle multiple streams at the same time, so
// the completion queue/server have a longer lifetime than the client(s).
class AsyncBidiGreeterServer {
 public:
  AsyncBidiGreeterServer() {
    // In general avoid setting up the server in the main thread (specifically,
    // in a constructor-like function such as this). We ignore this in the
    // context of an example.
    std::string server_address("0.0.0.0:50051");

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();

    // This initiates a single stream for a single client. To allow multiple
    // clients in different threads to connect, simply 'request' from the
    // different threads. Each stream is independent but can use the same
    // completion queue/context objects.
    stream_.reset(
        new ServerAsyncReaderWriter<HelloReply, HelloRequest>(&context_));
    service_.RequestSayHello(&context_, stream_.get(), cq_.get(), cq_.get(),
                             reinterpret_cast<void*>(Type::CONNECT));

    // This is important as the server should know when the client is done.
    context_.AsyncNotifyWhenDone(reinterpret_cast<void*>(Type::DONE));

    grpc_thread_.reset(new std::thread(
        (std::bind(&AsyncBidiGreeterServer::GrpcThread, this))));
    std::cout << "Server listening on " << server_address << std::endl;
  }

  void SetResponse(const std::string& response) {
    if (response == "quit" && IsRunning()) {
      stream_->Finish(grpc::Status::CANCELLED,
                      reinterpret_cast<void*>(Type::FINISH));
    }
    response_str_ = response;
  }

  ~AsyncBidiGreeterServer() {
    std::cout << "Shutting down server...." << std::endl;
    server_->Shutdown();
    // Always shutdown the completion queue after the server.
    cq_->Shutdown();
    grpc_thread_->join();
  }

  bool IsRunning() const { return is_running_; }

 private:
  void AsyncWaitForHelloRequest() {
    if (IsRunning()) {
      // In the case of the server, we wait for a READ first and then write a
      // response. A server cannot initiate a connection so the server has to
      // wait for the client to send a message in order for it to respond back.
      stream_->Read(&request_, reinterpret_cast<void*>(Type::READ));
    }
  }

  void AsyncHelloSendResponse() {
    std::cout << " ** Handling request: " << request_.name() << std::endl;
    HelloReply response;
    std::cout << " ** Sending response: " << response_str_ << std::endl;
    response.set_message(response_str_);
    stream_->Write(response, reinterpret_cast<void*>(Type::WRITE));
  }

  void GrpcThread() {
    while (true) {
      void* got_tag = nullptr;
      bool ok = false;
      if (!cq_->Next(&got_tag, &ok)) {
        std::cerr << "Server stream closed. Quitting" << std::endl;
        break;
      }

      if (ok) {
        std::cout << std::endl
                  << "**** Processing completion queue tag " << got_tag
                  << std::endl;
        switch (static_cast<Type>(reinterpret_cast<size_t>(got_tag))) {
          case Type::READ:
            std::cout << "Read a new message." << std::endl;
            AsyncHelloSendResponse();
            break;
          case Type::WRITE:
            std::cout << "Sending message (async)." << std::endl;
            AsyncWaitForHelloRequest();
            break;
          case Type::CONNECT:
            std::cout << "Client connected." << std::endl;
            AsyncWaitForHelloRequest();
            break;
          case Type::DONE:
            std::cout << "Server disconnecting." << std::endl;
            is_running_ = false;
            break;
          case Type::FINISH:
            std::cout << "Server quitting." << std::endl;
            break;
          default:
            std::cerr << "Unexpected tag " << got_tag << std::endl;
            GPR_ASSERT(false);
        }
      }
    }
  }

 private:
  HelloRequest request_;
  std::string response_str_ = "Default server response";
  ServerContext context_;
  std::unique_ptr<ServerCompletionQueue> cq_;
  MultiGreeter::AsyncService service_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<ServerAsyncReaderWriter<HelloReply, HelloRequest>> stream_;
  std::unique_ptr<std::thread> grpc_thread_;
  bool is_running_ = true;
};

int main(int argc, char** argv) {
  AsyncBidiGreeterServer server;

  std::string response;
  while (server.IsRunning()) {
    std::cout << "Enter next set of responses (type quit to end): ";
    std::cin >> response;
    server.SetResponse(response);
  }

  return 0;
}
