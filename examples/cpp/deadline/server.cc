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

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");

using grpc::CallbackServerContext;
using grpc::Channel;
using grpc::ClientContext;

using grpc::Server;
using grpc::ServerBidiReactor;
using grpc::ServerBuilder;
using grpc::ServerUnaryReactor;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

// Logic behind the server's behavior.
class GreeterServiceImpl final : public Greeter::CallbackService {
 public:
  GreeterServiceImpl(const std::string& self_address) {
    self_channel_ =
        grpc::CreateChannel(self_address, grpc::InsecureChannelCredentials());
  }

 private:
  ServerUnaryReactor* SayHello(CallbackServerContext* context,
                               const HelloRequest* request,
                               HelloReply* reply) override {
    if (absl::StartsWith(request->name(), "[propagate me]")) {
      std::unique_ptr<Greeter::Stub> stub = Greeter::NewStub(self_channel_);
      std::this_thread::sleep_for(std::chrono::milliseconds(800));
      // Forwarding this call to the self as a different call
      HelloRequest new_request;
      new_request.set_name(request->name().substr(14));
      std::unique_ptr<ClientContext> new_context =
          ClientContext::FromCallbackServerContext(*context);
      std::mutex mu;
      std::condition_variable cv;
      bool done = false;
      Status status;
      stub->async()->SayHello(new_context.get(), &new_request, reply,
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
      ServerUnaryReactor* reactor = context->DefaultReactor();
      reactor->Finish(status);
      return reactor;
    }

    if (request->name() == "delay") {
      // Intentionally delay for 1.5 seconds so that
      // the client will see deadline_exceeded.
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }

    reply->set_message(request->name());

    ServerUnaryReactor* reactor = context->DefaultReactor();
    reactor->Finish(Status::OK);
    return reactor;
  }

  std::shared_ptr<Channel> self_channel_;
};

void RunServer(uint16_t port) {
  std::string server_address = absl::StrFormat("0.0.0.0:%d", port);
  GreeterServiceImpl service(server_address);

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  RunServer(absl::GetFlag(FLAGS_port));
  return 0;
}
