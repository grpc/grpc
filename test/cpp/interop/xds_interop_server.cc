/*
 *
 * Copyright 2020 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <sstream>

#include "absl/flags/flag.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/gethostname.h"
#include "src/core/lib/transport/byte_stream.h"
#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(int32_t, port, 50051, "Server port.");
ABSL_FLAG(std::string, server_id, "cpp_server",
          "Server ID to include in responses.");

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::testing::Empty;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::TestService;

class TestServiceImpl : public TestService::Service {
 public:
  TestServiceImpl(const std::string& i) : hostname_(i) {}

  Status UnaryCall(ServerContext* context, const SimpleRequest* request,
                   SimpleResponse* response) override {
    response->set_server_id(absl::GetFlag(FLAGS_server_id));
    response->set_hostname(hostname_);
    context->AddInitialMetadata("hostname", hostname_);
    return Status::OK;
  }

  Status EmptyCall(ServerContext* context, const Empty* request,
                   Empty* response) override {
    context->AddInitialMetadata("hostname", hostname_);
    return Status::OK;
  }

 private:
  std::string hostname_;
};

void RunServer(const int port, const std::string& hostname) {
  std::ostringstream server_address;
  server_address << "0.0.0.0:" << port;

  TestServiceImpl service(hostname);
  ServerBuilder builder;
  builder.RegisterService(&service);
  builder.AddListeningPort(server_address.str(),
                           grpc::InsecureServerCredentials());
  std::unique_ptr<Server> server(builder.BuildAndStart());
  gpr_log(GPR_INFO, "Server listening on %s", server_address.str().c_str());

  server->Wait();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);

  char* hostname = grpc_gethostname();
  if (hostname == nullptr) {
    std::cout << "Failed to get hostname, terminating" << std::endl;
    return 1;
  }
  if (absl::GetFlag(FLAGS_port) == 0) {
    std::cout << "Invalid port, terminating" << std::endl;
    return 1;
  }

  RunServer(absl::GetFlag(FLAGS_port), hostname);

  return 0;
}
