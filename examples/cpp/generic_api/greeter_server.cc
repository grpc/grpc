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
#include <grpcpp/health_check_service_interface.h>

#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");

using grpc::ByteBuffer;
using grpc::CallbackGenericService;
using grpc::CallbackServerContext;
using grpc::GenericCallbackServerContext;
using grpc::ProtoBufferReader;
using grpc::ProtoBufferWriter;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerGenericBidiReactor;
using grpc::Status;
using grpc::StatusCode;
using helloworld::HelloReply;
using helloworld::HelloRequest;

// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public CallbackGenericService {
  ServerGenericBidiReactor* CreateReactor(
      GenericCallbackServerContext* context) override {
    if (context->method() == "/helloworld.Greeter/SayHello") {
      // Let the SayHello reactor handle this now on.
      return new SayHelloReactor();
    } else {
      // Forward this to the implementation of the base class returning
      // UNIMPLEMENTED.
      return CallbackGenericService::CreateReactor(context);
    }
  }

  class SayHelloReactor : public ServerGenericBidiReactor {
   public:
    SayHelloReactor() { StartRead(&request_); }

   private:
    Status OnSayHello(const HelloRequest& request, HelloReply* reply) {
      if (request.name() == "") {
        return Status(StatusCode::INVALID_ARGUMENT, "name is not specified");
      }
      reply->set_message(absl::StrFormat("Hello %s", request.name()));
      return Status::OK;
    }

    void OnDone() override { delete this; }
    void OnReadDone(bool ok) override {
      if (!ok) {
        return;
      }
      Status result;
      // Deserialize a request message
      HelloRequest request;
      result = grpc::GenericDeserialize<ProtoBufferReader, HelloRequest>(
          &request_, &request);
      if (!result.ok()) {
        Finish(result);
        return;
      }
      // Call the SayHello handler
      HelloReply reply;
      result = OnSayHello(request, &reply);
      if (!result.ok()) {
        Finish(result);
        return;
      }
      // Serialize a reply message
      bool own_buffer;
      result = grpc::GenericSerialize<ProtoBufferWriter, HelloReply>(
          reply, &response_, &own_buffer);
      if (!result.ok()) {
        Finish(result);
        return;
      }
      StartWrite(&response_);
    }
    void OnWriteDone(bool ok) override {
      Finish(ok ? Status::OK
                : Status(StatusCode::UNKNOWN, "Unexpected failure"));
    }
    ByteBuffer request_;
    ByteBuffer response_;
  };

 private:
  absl::Mutex mu_;
};

void RunServer(uint16_t port) {
  std::string server_address = absl::StrFormat("0.0.0.0:%d", port);
  GreeterServiceImpl service;
  grpc::EnableDefaultHealthCheckService(true);
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterCallbackGenericService(&service);
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
