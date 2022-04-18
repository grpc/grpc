/*
 *
 * Copyright 2018 gRPC authors.
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
#include <sstream>

#include "absl/flags/flag.h"

#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/service_type.h>
#include <grpcpp/server_builder.h>

#include "test/core/tsi/alts/fake_handshaker/fake_handshaker_server.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(int32_t, handshaker_port, 55056,
          "TCP port on which the fake handshaker server listens to.");

static void RunFakeHandshakerServer(const std::string& server_address) {
  std::unique_ptr<grpc::Service> service =
      grpc::gcp::CreateFakeHandshakerService(
          0 /* expected max concurrent rpcs unset */);
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(service.get());
  gpr_log(GPR_INFO, "Fake handshaker server listening on %s",
          server_address.c_str());
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  server->Wait();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);

  GPR_ASSERT(absl::GetFlag(FLAGS_handshaker_port) != 0);
  std::ostringstream server_address;
  server_address << "[::1]:" << absl::GetFlag(FLAGS_handshaker_port);

  RunFakeHandshakerServer(server_address.str());
  return 0;
}
