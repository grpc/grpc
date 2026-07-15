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

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
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

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");
ABSL_FLAG(size_t, quota, 20, "Resource quota, in megabytes");

namespace {

//
// Server reactor that is slow to read incoming messages, causing the buffers
// to fill.
//
class SlowReadingBidiReactor final
    : public grpc::ServerBidiReactor<helloworld::HelloRequest,
                                     helloworld::HelloReply> {
 public:
  SlowReadingBidiReactor() { StartRead(&req_); }

  void OnReadDone(bool ok) override {
    std::cout << "Received request with " << req_.name().length()
              << " bytes name\n";
    if (!ok) {
      Finish(grpc::Status::OK);
      return;
    }
    sleep(1);
    StartRead(&req_);
  }

  void OnDone() override {
    std::cout << "Done\n";
    delete this;
  }

 private:
  absl::Mutex mu_;
  helloworld::HelloRequest req_;
};

// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public helloworld::Greeter::CallbackService {
  grpc::ServerBidiReactor<helloworld::HelloRequest, helloworld::HelloReply>*
  SayHelloBidiStream(grpc::CallbackServerContext* /* context */) override {
    return new SlowReadingBidiReactor();
  }
};

}  // namespace

void RunServer(uint16_t port) {
  std::string server_address = absl::StrFormat("0.0.0.0:%d", port);
  GreeterServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  grpc::ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  grpc::ResourceQuota quota;
  quota.Resize(absl::GetFlag(FLAGS_quota) * 1024 * 1024);
  // Finally assemble the server.
  auto server = builder.BuildAndStart();
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  RunServer(absl::GetFlag(FLAGS_port));
  return 0;
}
