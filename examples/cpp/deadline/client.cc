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

#include <grpcpp/grpcpp.h>

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_cat.h"

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(std::string, target, "localhost:50051", "Server address");

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::StatusCode;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

void unaryCall(std::shared_ptr<Channel> channel, std::string label,
               std::string message, grpc::StatusCode expected_code) {
  std::unique_ptr<Greeter::Stub> stub = Greeter::NewStub(channel);

  // Data we are sending to the server.
  HelloRequest request;
  request.set_name(message);

  // Container for the data we expect from the server.
  HelloReply reply;

  // Context for the client. It could be used to convey extra information to
  // the server and/or tweak certain RPC behaviors.
  ClientContext context;

  // Set 1 second timeout
  context.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::seconds(1));

  // The actual RPC.
  std::mutex mu;
  std::condition_variable cv;
  bool done = false;
  Status status;
  stub->async()->SayHello(&context, &request, &reply,
                          [&mu, &cv, &done, &status](Status s) {
                            status = std::move(s);
                            std::lock_guard<std::mutex> lock(mu);
                            done = true;
                            cv.notify_one();
                          });

  std::unique_lock<std::mutex> lock(mu);
  while (!done) {
    cv.wait(lock);
  }

  // Act upon its status.
  std::cout << "[" << label << "] wanted = " << expected_code
            << ", got = " << status.error_code() << std::endl;
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  std::string target_str = absl::GetFlag(FLAGS_target);
  // We indicate that the channel isn't authenticated (use of
  // InsecureChannelCredentials()).
  std::shared_ptr<Channel> channel =
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials());
  // Making test calls
  unaryCall(channel, "Successful request", "world", grpc::StatusCode::OK);
  unaryCall(channel, "Exceeds deadline", "delay",
            grpc::StatusCode::DEADLINE_EXCEEDED);
  unaryCall(channel, "Successful request with propagated deadline",
            "[propagate me]world", grpc::StatusCode::OK);
  unaryCall(channel, "Exceeds propagated deadline",
            "[propagate me][propagate me]world",
            grpc::StatusCode::DEADLINE_EXCEEDED);
  return 0;
}
