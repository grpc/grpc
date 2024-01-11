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

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#include "google/rpc/error_details.pb.h"

#include "src/proto/grpc/status/status.pb.h"
#else
#include "error_details.pb.h"
#include "helloworld.grpc.pb.h"
#include "status.pb.h"
#endif

ABSL_FLAG(std::string, target, "localhost:50051", "Server address");

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}

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
    stub_->async()->SayHello(&context, &request, &reply, [&](Status s) {
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
      PrintErrorDetails(status);
    }
  }

  void PrintErrorDetails(grpc::Status status) {
    auto error_details = status.error_details();
    if (error_details.empty()) {
      return;
    }
    // If error_details are present in the status, this tries to deserialize
    // those assuming they're proto messages.
    google::rpc::Status s;
    if (!s.ParseFromString(error_details)) {
      std::cout << "Failed to deserialize `error_details`" << std::endl;
      return;
    }
    std::cout << absl::StrFormat("Details:") << std::endl;
    for (auto& detail : s.details()) {
      google::rpc::QuotaFailure quota_failure;
      if (detail.UnpackTo(&quota_failure)) {
        for (auto& violation : quota_failure.violations()) {
          std::cout << absl::StrFormat("- Quota: subject=%s description=%s",
                                       violation.subject(),
                                       violation.description())
                    << std::endl;
        }
      } else {
        std::cout << "Unknown error_detail: " + detail.type_url() << std::endl;
      }
    }
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  std::string target_str = absl::GetFlag(FLAGS_target);
  // We indicate that the channel isn't authenticated (use of
  // InsecureChannelCredentials()).
  GreeterClient greeter(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  // Sends a first new name, expecting OK
  greeter.SayHello("World");
  // Sends a duplicate name, expecting RESOURCE_EXHAUSTED with error_details
  greeter.SayHello("World");
  return 0;
}
