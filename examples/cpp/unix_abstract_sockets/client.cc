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

#include <grpcpp/grpcpp.h>

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

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

class GreeterClient {
 public:
  explicit GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}

  std::string SayHello(const std::string& user) {
    HelloRequest request;
    request.set_name(user);
    HelloReply reply;
    ClientContext context;
    Status status = stub_->SayHello(&context, request, &reply);
    if (status.ok()) {
      return reply.message();
    }
    std::cout << status.error_code() << ": " << status.error_message()
              << std::endl;
    return "RPC failed";
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  std::string socket_type = absl::GetFlag(FLAGS_socket_type);
  std::string target;
  if (socket_type == "path") {
    // Path-based: connects to a socket file on the filesystem.
    // The file must exist (server must be running).
    target = "unix:/tmp/grpc_example.sock";
  } else {
    // Abstract: connects via kernel namespace — no file needed.
    // Linux-only. Fails if server is not running (namespace entry disappears).
    target = "unix-abstract:grpc%00abstract";
  }

  GreeterClient greeter(
      grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));

  std::cout << "Connecting to: " << target << std::endl;
  std::string reply = greeter.SayHello("hello from client");
  std::cout << "Response: " << reply << std::endl;

  return 0;
}
