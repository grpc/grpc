/*
 *
 * Copyright 2023 gRPC authors.
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
 *
 */
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#include "examples/protos/route_guide.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#include "route_guide.grpc.pb.h"
#endif

ABSL_FLAG(std::string, target, "localhost:50051", "Server address");

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  std::string target_str = absl::GetFlag(FLAGS_target);
  // We indicate that the channel isn't authenticated (use of
  // InsecureChannelCredentials()).
  auto channel =
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials());

  std::mutex mu;
  std::condition_variable cv;
  int done_count = 0;

  // Callbacks will be called on background threads
  std::unique_lock<std::mutex> lock(mu);

  ClientContext hello_context;
  helloworld::HelloRequest hello_request;
  helloworld::HelloReply hello_response;
  Status hello_status;

  ClientContext feature_context;
  routeguide::Point feature_request;
  routeguide::Feature feature_response;
  Status feature_status;
  // Request to a Greeter service
  hello_request.set_name("user");
  helloworld::Greeter::NewStub(channel)->async()->SayHello(
      &hello_context, &hello_request, &hello_response, [&](Status status) {
        std::lock_guard<std::mutex> lock(mu);
        done_count++;
        hello_status = std::move(status);
        cv.notify_all();
      });
  // Request to a RouteGuide service
  feature_request.set_latitude(50);
  feature_request.set_longitude(100);
  routeguide::RouteGuide::NewStub(channel)->async()->GetFeature(
      &feature_context, &feature_request, &feature_response,
      [&](Status status) {
        std::lock_guard<std::mutex> lock(mu);
        done_count++;
        feature_status = std::move(status);
        cv.notify_all();
      });
  // Wait for both requests to finish
  cv.wait(lock, [&]() { return done_count == 2; });
  if (hello_status.ok()) {
    std::cout << "Greeter received: " << hello_response.message() << std::endl;
  } else {
    std::cerr << "Greeter failed: " << hello_status.error_message()
              << std::endl;
  }
  if (feature_status.ok()) {
    std::cout << "Found feature: " << feature_response.name() << std::endl;
  } else {
    std::cerr << "Getting feature failed: " << feature_status.error_message()
              << std::endl;
  }
  return 0;
}
