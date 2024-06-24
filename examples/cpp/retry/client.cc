/*
 * Copyright 2024 gRPC authors.
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
 */

#include <iostream>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

constexpr absl::string_view kTargetAddress = "localhost:50052";

// clang-format off
constexpr absl::string_view kRetryPolicy =
    "{\"methodConfig\" : [{"
    "   \"name\" : [{\"service\": \"helloworld.Greeter\"}],"
    "   \"waitForReady\": true,"
    "   \"retryPolicy\": {"
    "     \"maxAttempts\": 4,"
    "     \"initialBackoff\": \"1s\","
    "     \"maxBackoff\": \"120s\","
    "     \"backoffMultiplier\": 1.0,"
    "     \"retryableStatusCodes\": [\"UNAVAILABLE\"]"
    "    }"
    "}]}";
// clang-format on

class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  std::string SayHello(const std::string& user) {
    // Data we are sending to the server.
    HelloRequest request;
    request.set_name(user);
    // Container for the data we expect from the server.
    HelloReply reply;
    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;
    // The actual RPC.
    Status status = stub_->SayHello(&context, request, &reply);
    // Act upon its status.
    if (status.ok()) {
      return reply.message();
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return "RPC failed";
    }
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

int main() {
  auto channel_args = grpc::ChannelArguments();
  channel_args.SetServiceConfigJSON(std::string(kRetryPolicy));
  GreeterClient greeter(grpc::CreateCustomChannel(
      std::string(kTargetAddress), grpc::InsecureChannelCredentials(),
      channel_args));
  std::string user("world");
  std::string reply = greeter.SayHello(user);
  std::cout << "Greeter received: " << reply << std::endl;
  return 0;
}
