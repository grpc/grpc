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

#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/string_view.h"

#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/status.h>

#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "test/core/util/test_config.h"

ABSL_FLAG(std::string, target, "", "Target host:port");
ABSL_FLAG(bool, secure, false, "Use SSL Credentials");

void UnaryCall() {
  // Set the authentication mechanism.
  std::shared_ptr<grpc::ChannelCredentials> creds =
      grpc::InsecureChannelCredentials();
  if (absl::GetFlag(FLAGS_secure)) {
    // TODO (chennancy) Add in secure credentials
    gpr_log(GPR_INFO, "Supposed to be secure, is not yet");
  }

  // Create a channel to the server and a stub
  std::shared_ptr<grpc::Channel> channel =
      CreateChannel(absl::GetFlag(FLAGS_target), creds);
  std::unique_ptr<grpc::testing::BenchmarkService::Stub> stub =
      grpc::testing::BenchmarkService::NewStub(channel);

  // Start a call.
  struct CallParams {
    grpc::ClientContext context;
    grpc::testing::SimpleRequest request;
    grpc::testing::SimpleResponse response;
  };
  CallParams* params = new CallParams();
  stub->async()->UnaryCall(&params->context, &params->request,
                           &params->response, [](const grpc::Status& status) {
                             if (status.ok()) {
                               gpr_log(GPR_INFO, "UnaryCall RPC succeeded.");
                             } else {
                               gpr_log(GPR_ERROR, "UnaryCall RPC failed.");
                             }
                           });
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  char* fake_argv[1];
  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc::testing::TestEnvironment env(&argc, argv);
  if (absl::GetFlag(FLAGS_target).empty()) {
    gpr_log(GPR_ERROR, "Client: No target port entered");
    return 1;
  }
  gpr_log(GPR_INFO, "Client Target: %s", absl::GetFlag(FLAGS_target).c_str());

  UnaryCall();
  gpr_log(GPR_INFO, "Client Done");
  return 0;
}
