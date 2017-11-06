/*
 *
 * Copyright 2015 gRPC authors.
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

#include <gflags/gflags.h>
#include <iostream>
#include <memory>
#include <string>

#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/support/log.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"

DEFINE_string(address, "", "Address to bind to");

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

namespace grpc {
namespace testing {

class ServiceImpl final : public ::grpc::testing::EchoTestService::Service {
  Status BidiStream(
      ServerContext* context,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override {
    EchoRequest request;
    EchoResponse response;
    while (stream->Read(&request)) {
      gpr_log(GPR_INFO, "recv msg %s", request.message().c_str());
      response.set_message(request.message());
      stream->Write(response);
    }
    return Status::OK;
  }
};

void RunServer() {
  ServiceImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(FLAGS_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << FLAGS_address << std::endl;
  server->Wait();
}
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ParseCommandLineFlags(&argc, &argv, true);
  grpc::testing::RunServer();

  return 0;
}
