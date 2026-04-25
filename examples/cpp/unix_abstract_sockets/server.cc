// Copyright 2021 the gRPC authors.
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

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <iostream>
#include <memory>
#include <string>

#include "examples/protos/helloworld.grpc.pb.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"

ABSL_FLAG(std::string, socket_type, "abstract",
          "Socket type to use: 'abstract' (Linux-only, no filesystem file) "
          "or 'path' (filesystem socket, cross-platform)");

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

class GreeterServiceImpl final : public Greeter::Service {
  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    // peer() shows the connected client's address.
    // For unbound UDS clients (typical), this is a synthetic unique string
    // derived from the server's listener address + a per-connection counter,
    // e.g. "unix:/tmp/grpc_example.sock_0" or "unix-abstract:grpc%00abstract_0"
    std::cout << "  peer()  : " << context->peer() << std::endl;
    std::cout << "  request : " << request->name() << std::endl;
    reply->set_message(request->name());
    return Status::OK;
  }
};

void RunServer(const std::string& server_address) {
  GreeterServiceImpl service;
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on: " << server_address << std::endl;
  std::cout << "Waiting for connections..." << std::endl;
  server->Wait();
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  std::string socket_type = absl::GetFlag(FLAGS_socket_type);
  std::string server_address;
  if (socket_type == "path") {
    // Path-based: creates a real file on the filesystem.
    // Visible via `ls /tmp/grpc_example.sock`, must be deleted on cleanup.
    server_address = "unix:/tmp/grpc_example.sock";
  } else {
    // Abstract: lives in kernel namespace only, no filesystem file.
    // Auto-cleaned when the process exits. Linux-only.
    server_address = "unix-abstract:grpc%00abstract";
  }

  RunServer(server_address);
  return 0;
}
