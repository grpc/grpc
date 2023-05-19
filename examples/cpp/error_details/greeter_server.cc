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

#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#include "google/rpc/error_details.pb.h"
#include "src/proto/grpc/status/status.pb.h"
#else
#include "helloworld.grpc.pb.h"
#include "error_details.pb.h"
#include "status.pb.h"
#endif

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerUnaryReactor;
using grpc::Status;
using grpc::StatusCode;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public Greeter::CallbackService {
  ServerUnaryReactor* SayHello(CallbackServerContext* context,
                               const HelloRequest* request,
                               HelloReply* reply) override {
    ServerUnaryReactor* reactor = context->DefaultReactor();
    Status status;
    // Checks whether it is a duplicate request
    if (CheckRequestDuplicate(request->name())) {
      // Returns an error status with more detailed information.
      // In this example, the status has additional google::rpc::QuotaFailure
      // conveying additional information about the error.
      google::rpc::QuotaFailure quota_failure;
      auto violation = quota_failure.add_violations();
      violation->set_subject("name: " + request->name());
      violation->set_description("Limit one greeting per person");
      google::rpc::Status s;
      s.set_code(static_cast<int>(StatusCode::RESOURCE_EXHAUSTED));
      s.set_message("Request limit exceeded");
      s.add_details()->PackFrom(quota_failure);
      status = Status(StatusCode::RESOURCE_EXHAUSTED, "Request limit exceeded",
                      s.SerializeAsString());
    } else {
      reply->set_message(absl::StrFormat("Hello %s", request->name()));
      status = Status::OK;
    }
    reactor->Finish(status);
    return reactor;
  }

 private:
  bool CheckRequestDuplicate(const std::string& name) {
    absl::MutexLock lock(&mu_);
    return !request_name_set_.insert(name).second;
  }

  absl::Mutex mu_;
  std::unordered_set<std::string> request_name_set_;
};

void RunServer(uint16_t port) {
  std::string server_address = absl::StrFormat("0.0.0.0:%d", port);
  GreeterServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
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
