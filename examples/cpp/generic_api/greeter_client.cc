// Copyright 2023 gRPC authors.
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

#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/grpcpp.h>

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/strings/str_format.h"

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(std::string, target, "localhost:50051", "Server address");

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

using ProtoGenericStub =
    ::grpc::TemplatedGenericStub<::google::protobuf::Message,
                                 ::google::protobuf::Message>;

class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(new ProtoGenericStub(channel)) {}

  // Assembles the client's payload, sends it and prints the response back
  // from the server.
  void SayHello(const std::string& user) {
    // Data we are sending to the server.
    HelloRequest request;
    request.set_name(user);
    // Container for the data we expect from the server.
    HelloReply reply;
    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;
    // The actual RPC.
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    Status status;
    std::cout << absl::StrFormat("### Send: SayHello(name=%s)", user)
              << std::endl;
    // Send a unary call using a generic stub. Unlike generated subs,
    // this requires to specify the name of call.
    stub_->UnaryCall(&context, "/helloworld.Greeter/SayHello",
                     grpc::StubOptions(), &request, &reply, [&](Status s) {
                       status = std::move(s);
                       std::lock_guard<std::mutex> lock(mu);
                       done = true;
                       cv.notify_one();
                     });
    std::unique_lock<std::mutex> lock(mu);
    while (!done) {
      cv.wait(lock);
    }
    // Handles the reply
    if (status.ok()) {
      std::cout << absl::StrFormat("Ok. ReplyMessage=%s", reply.message())
                << std::endl;
    } else {
      std::cout << absl::StrFormat("Failed. Code=%d Message=%s",
                                   status.error_code(), status.error_message())
                << std::endl;
    }
  }

 private:
  // Instead of `Greeter::Stub`, it uses `ProtoGenericStub` to send any calls.
  std::unique_ptr<ProtoGenericStub> stub_;
};

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  std::string target_str = absl::GetFlag(FLAGS_target);
  // We indicate that the channel isn't authenticated (use of
  // InsecureChannelCredentials()).
  GreeterClient greeter(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  greeter.SayHello("World");
  greeter.SayHello("gRPC");
  return 0;
}
