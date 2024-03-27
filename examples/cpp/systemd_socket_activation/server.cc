// Copyright 2022 the gRPC authors.
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

#include <signal.h>

#include <iostream>
#include <memory>
#include <string>

#include "examples/protos/helloworld.grpc.pb.h"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public Greeter::Service {
  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    std::string prefix("Hello ");
    reply->set_message(prefix + request->name());
    std::cout << "Client connected from " << context->peer() << std::endl;
    return Status::OK;
  }
};

void* wait_for_sigint_then_shutdown(void* data) {
  Server** server = static_cast<Server**>(data);

  int signum = 0;
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigwait(&sigset, &signum);

  std::cout << "SIGINT received." << std::endl;

  if (server && *server) {
    std::cout << "Shut down server..." << std::endl;
    (*server)->Shutdown();
  }
  return nullptr;
}

void RunServer(std::string server_address, bool expand_wildcard_addr) {
  // Inhibit SIGINT in the main thread and all future threads
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  pthread_sigmask(SIG_BLOCK, &sigset, nullptr);

  // To share the server pointer to the thread
  Server* _server = nullptr;

  // Create a separate thread to wait on SIGINT to trigger server shutdown
  pthread_t signal_thread;
  pthread_create(&signal_thread, nullptr, wait_for_sigint_then_shutdown,
                 &_server);

  // gRPC service
  GreeterServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;

  // Activate wildcard address expansion if requested
  if (expand_wildcard_addr) {
    builder.AddChannelArgument("grpc.expand_wildcard_addrs", 1);
  }

  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);

  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // update the server pointer in the thread
  _server = server.get();

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();

  // waiting for thread to terminate
  pthread_join(signal_thread, nullptr);

  std::cout << "Server finished" << std::endl;
}

int main(int argc, char** argv) {
  // Argument "--listen=" designates the interface to listen on
  std::string listen_str = "unix:///tmp/server";
  std::string arg_listen_str("--listen");

  // Argument "--expand-wildcard-addr" requests wildcard addr
  // (0.0.0.0 and [::]) expansion into each local interface addr
  bool expand_wildcard_addr = false;
  std::string arg_expand_wilcard_str("--expand-wildcard-addr");

  for (int i = 1; i < argc; i++) {
    std::string arg_val = argv[i];
    // listen arg
    size_t start_pos = arg_val.find(arg_listen_str);
    if (start_pos != std::string::npos) {
      start_pos += arg_listen_str.size();
      if (arg_val[start_pos] == '=') {
        listen_str = arg_val.substr(start_pos + 1);
      }
    }
    // expand arg
    start_pos = arg_val.find(arg_expand_wilcard_str);
    if (start_pos != std::string::npos) {
      expand_wildcard_addr = true;
    }
  }

  RunServer(listen_str, expand_wildcard_addr);

  return 0;
}
