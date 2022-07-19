/*
 *
 * Copyright 2022 gRPC authors.
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

#include <stdio.h>
#include <string.h>

#include <thread>

#include <gtest/gtest.h>

#include "absl/algorithm/container.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "util/logging.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/client_callback.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "test/core/memory_usage/memstats.h"
#include "test/core/util/port.h"
#include "test/core/util/subprocess.h"
#include "test/core/util/test_config.h"

class ServerCallbackImpl final
    : public grpc::testing::BenchmarkService::CallbackService {
  grpc::ServerUnaryReactor* UnaryCall(
      grpc::CallbackServerContext* context,
      const grpc::testing::SimpleRequest* request,
      grpc::testing::SimpleResponse* response) override {
    gpr_log(GPR_INFO, "RPC CALL RECEIVED");

    auto* reactor = context->DefaultReactor();
    reactor->Finish(grpc::Status::OK);
    return reactor;
  }
};

static void sigint_handler(int /*x*/) { _exit(0); }

ABSL_FLAG(std::string, bind, "", "Bind host:port");
ABSL_FLAG(bool, secure, false, "Use SSL Credentials");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  char* fake_argv[1];

  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc::testing::TestEnvironment env(&argc, argv);

  grpc_init();
  std::string server_address = absl::GetFlag(FLAGS_bind);
  gpr_log(GPR_INFO, "Server port: %s", server_address.c_str());

  ServerCallbackImpl callback_server;
  grpc::ServerBuilder builder;

  // Set the authentication mechanism.
  std::shared_ptr<grpc::ServerCredentials> creds =
      grpc::InsecureServerCredentials();
  if (absl::GetFlag(FLAGS_secure)) {
    gpr_log(GPR_INFO, "Supposed to be secure, is not yet");
    // TODO (chennancy) Add in secure credentials
  }
  builder.AddListeningPort(server_address, creds);

  // Register "service" as the instance through which we'll communicate with
  // clients.
  builder.RegisterService(&callback_server);

  signal(SIGINT, sigint_handler);
  // Set up the server to start accepting requests.
  std::shared_ptr<grpc::Server> server(builder.BuildAndStart());
  gpr_log(GPR_INFO, "Server listening on %s", server_address.c_str());

  // Keep the program running until the server shuts down.
  server->Wait();
  // TODO (chennancy) Add graceful shutdown
  return 0;
}
