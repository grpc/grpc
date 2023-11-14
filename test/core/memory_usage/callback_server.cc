//
//
// Copyright 2022 gRPC authors.
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
//
//

#include <signal.h>
#include <unistd.h>

#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/support/server_callback.h>
#include <grpcpp/support/status.h>
#include <grpcpp/xds_server_builder.h>

#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "test/core/memory_usage/memstats.h"
#include "test/core/util/test_config.h"

ABSL_FLAG(std::string, bind, "", "Bind host:port");
ABSL_FLAG(bool, secure, false, "Use SSL Credentials");
ABSL_FLAG(bool, use_xds, false, "Use xDS");

class ServerCallbackImpl final
    : public grpc::testing::BenchmarkService::CallbackService {
 public:
  explicit ServerCallbackImpl(long before_server_memory)
      : before_server_create(before_server_memory) {}

  grpc::ServerUnaryReactor* UnaryCall(
      grpc::CallbackServerContext* context,
      const grpc::testing::SimpleRequest* /* request */,
      grpc::testing::SimpleResponse* /* response */) override {
    auto* reactor = context->DefaultReactor();
    reactor->Finish(grpc::Status::OK);
    return reactor;
  }
  grpc::ServerUnaryReactor* GetBeforeSnapshot(
      grpc::CallbackServerContext* context,
      const grpc::testing::SimpleRequest* /* request */,
      grpc::testing::MemorySize* response) override {
    gpr_log(GPR_INFO, "BeforeSnapshot RPC CALL RECEIVED");
    response->set_rss(before_server_create);
    auto* reactor = context->DefaultReactor();
    reactor->Finish(grpc::Status::OK);
    return reactor;
  }

 private:
  long before_server_create;
};

// We have some sort of deadlock, so let's not exit gracefully for now.
// TODO(chennancy) Add graceful shutdown
static void sigint_handler(int /*x*/) { _exit(0); }

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  char* fake_argv[1];
  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  signal(SIGINT, sigint_handler);
  std::string server_address = absl::GetFlag(FLAGS_bind);
  if (server_address.empty()) {
    gpr_log(GPR_ERROR, "Server: No port entered");
    return 1;
  }
  gpr_log(GPR_INFO, "Server port: %s", server_address.c_str());

  // Get initial process memory usage before creating server
  long before_server_create = GetMemUsage();
  ServerCallbackImpl callback_server(before_server_create);

  grpc::XdsServerBuilder xds_builder;
  grpc::ServerBuilder normal_builder;
  grpc::ServerBuilder* builder =
      absl::GetFlag(FLAGS_use_xds) ? &xds_builder : &normal_builder;

  // Set the authentication mechanism.
  std::shared_ptr<grpc::ServerCredentials> creds =
      grpc::InsecureServerCredentials();
  if (absl::GetFlag(FLAGS_secure)) {
    gpr_log(GPR_INFO, "Supposed to be secure, is not yet");
    // TODO (chennancy) Add in secure credentials
  }
  builder->AddListeningPort(server_address, creds);
  builder->RegisterService(&callback_server);

  // Set up the server to start accepting requests.
  std::shared_ptr<grpc::Server> server(builder->BuildAndStart());
  gpr_log(GPR_INFO, "Server listening on %s", server_address.c_str());

  // Keep the program running until the server shuts down.
  server->Wait();
  return 0;
}
