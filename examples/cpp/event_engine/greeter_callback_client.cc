/*
 *
 * Copyright 2021 gRPC authors.
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

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"

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

using namespace std::literals;

class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}

  std::string SayHello(const std::string& user) {
    HelloRequest request;
    request.set_name(user);
    HelloReply reply;
    ClientContext context;
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    Status status;
    stub_->async()->SayHello(&context, &request, &reply,
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

// Returns when the only shared EventEngine instance is owned by this function.
//
// usage: WaitForSingleOwner(std::move(engine));
//
// Note that all channels, stubs, and other gRPC application objects must be
// destroyed. They each hold EventEngine references.
template <typename T>
void WaitForSingleOwner(std::shared_ptr<T>&& sp) {
  std::cout << "Waiting for gRPC to be done using the EventEngine" << std::endl;
  while (true) {
    absl::SleepFor(absl::Milliseconds(500));
    if (sp.use_count() == 1) {
      break;
    }
    std::cout << "Current EventEngine use count: " << sp.use_count()
              << std::endl;
  }
}

int main(int argc, char** argv) {
  // Have the application own an instance of the built-in EventEngine.
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine =
      grpc_event_engine::experimental::CreateEventEngine();
  // Set a custom factory so that all requests for a new EventEngine will return
  // this application-owned engine instance.
  grpc_event_engine::experimental::SetEventEngineFactory([&engine] {
    std::cout << "Calling the custom EventEngine factory" << std::endl;
    return engine;
  });
  // Here is some arbitrary, application-specific use of the EventEngine API.
  engine->RunAfter(
      2s, [engine] { std::cout << "Application timer fired!" << std::endl; });
  absl::ParseCommandLine(argc, argv);
  {
    std::string target_str = absl::GetFlag(FLAGS_target);
    GreeterClient greeter(
        grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
    std::string user("EventEngine");
    std::string reply = greeter.SayHello(user);
    std::cout << "Greeter received: " << reply << std::endl;
  }
  WaitForSingleOwner(std::move(engine));
  return 0;
}
