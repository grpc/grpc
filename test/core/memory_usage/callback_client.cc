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
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/impl/codegen/time.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/status.h>

#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "test/core/memory_usage/memstats.h"
#include "test/core/util/test_config.h"

ABSL_FLAG(std::string, target, "", "Target host:port");
ABSL_FLAG(bool, secure, false, "Use SSL Credentials");
ABSL_FLAG(int, server_pid, 99999, "Server's pid");
ABSL_FLAG(int, size, 500, "Number of channels"); //TODO(chennancy) Pass in the real amount of flags

std::shared_ptr<grpc::Channel> CreateChannelForTest(int index) {
  // Set the authentication mechanism.
  std::shared_ptr<grpc::ChannelCredentials> creds =
      grpc::InsecureChannelCredentials();
  if (absl::GetFlag(FLAGS_secure)) {
    // TODO (chennancy) Add in secure credentials
    gpr_log(GPR_INFO, "Supposed to be secure, is not yet");
  }
  
  //Channel args to prevent connection from closing after RPC is done
  grpc::ChannelArguments channel_args;
  channel_args.SetInt(GRPC_ARG_MAX_CONNECTION_IDLE_MS, INT_MAX);
  channel_args.SetInt(GRPC_ARG_MAX_CONNECTION_AGE_MS, INT_MAX);
  //Arg to bypass mechanism that combines channels on the serverside if they have the same channel args. Allows for one channel per connection
  channel_args.SetInt("grpc.memory_usage_counter", index);

  // Create a channel to the server and a stub
  std::shared_ptr<grpc::Channel> channel =
      CreateCustomChannel(absl::GetFlag(FLAGS_target), creds, channel_args);
  return channel;
}

void UnaryCall(std::shared_ptr<grpc::Channel> channel) {
  std::unique_ptr<grpc::testing::BenchmarkService::Stub> stub = grpc::testing::BenchmarkService::NewStub(channel);

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

// Get memory usage of server's process before the server is made
void GetBeforeSnapshot(std::shared_ptr<grpc::Channel> channel, long& before_server_memory) {
  std::unique_ptr<grpc::testing::BenchmarkService::Stub> stub = grpc::testing::BenchmarkService::NewStub(channel);

  // Start a call.
  struct CallParams {
    grpc::ClientContext context;
    grpc::testing::SimpleRequest request;
    grpc::testing::MemorySize response;
  };
  CallParams* params = new CallParams();
  stub->async()->GetBeforeSnapshot(
      &params->context, &params->request, &params->response,
      [params, &before_server_memory](const grpc::Status& status) {
        if (status.ok()) {
          before_server_memory=params->response.rss();
          gpr_log(GPR_INFO, "Server Before RPC: %ld", params->response.rss());
          gpr_log(GPR_INFO, "GetBeforeSnapshot succeeded.");
        } else {
          gpr_log(GPR_ERROR, "GetBeforeSnapshot failed.");
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

  // Getting initial memory usage
  std::shared_ptr<grpc::Channel> get_memory_channel= CreateChannelForTest(0);
  long before_server_memory;
  GetBeforeSnapshot(get_memory_channel, before_server_memory);
  long before_client_memory = GetMemUsage();

  int size = absl::GetFlag(FLAGS_size);
  std::vector<std::shared_ptr<grpc::Channel>> channels_list(size);
  for(int i=0; i<size; ++i){
    std::shared_ptr<grpc::Channel> channel= CreateChannelForTest(i);
    channels_list[i]=channel;
    UnaryCall(channel);
  }
  //gpr_sleep_until(grpc_timeout_seconds_to_deadline(10));

  // Getting peak memory usage
  long peak_server_memory = GetMemUsage(absl::GetFlag(FLAGS_server_pid));
  long peak_client_memory = GetMemUsage();
  gpr_log(GPR_INFO, "Before Server Mem: %ld", before_server_memory);
  gpr_log(GPR_INFO, "Before Client Mem: %ld", before_client_memory);
  gpr_log(GPR_INFO, "Peak Server Mem: %ld", peak_server_memory);
  gpr_log(GPR_INFO, "Peak Client Mem: %ld", peak_client_memory);
  gpr_log(GPR_INFO, "Server Difference: %ld", peak_server_memory-before_server_memory);
  gpr_log(GPR_INFO, "Client Difference: %ld", peak_client_memory-before_client_memory);

  //Checking if any channels shutdown
  for(int i=0; i<size; ++i){
    GPR_ASSERT(!channels_list[i]->WaitForStateChange(GRPC_CHANNEL_READY, absl::Now() + absl::Milliseconds(1)));
  }
  gpr_log(GPR_INFO, "Client Done");
  return 0;
}
