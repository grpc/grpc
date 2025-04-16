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

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "wrapping_event_engine.h"

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

namespace my_application {

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

}  // namespace my_application

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  // Create some EventEngine of your choosing, likely your own.
  auto custom_engine = std::make_shared<my_application::WrappingEventEngine>();
  // Provide this engine to gRPC. Now there are 2 refs to this engine: one here,
  // and one owned by gRPC.
  grpc_event_engine::experimental::SetDefaultEventEngine(custom_engine);
  // This scope ensures that gRPC objects are destroyed before trying to shut
  // down the EventEngine.
  {
    std::string target_str = absl::GetFlag(FLAGS_target);
    my_application::GreeterClient greeter(
        grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
    std::string user("EventEngine");
    std::string reply = greeter.SayHello(user);
    std::cout << "Greeter received: " << reply << std::endl;
  }
  LOG(INFO) << "My EventEngine ran " << custom_engine->get_run_count()
            << " closures";
  // Release the application's ownership of the EventEngine. Now gRPC solely
  // owns the engine.
  custom_engine.reset();
  // Block until gRPC is done using the engine, and the engine is destroyed.
  grpc_event_engine::experimental::ShutdownDefaultEventEngine();
  return 0;
}
