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

#include <limits.h>
#include <stdio.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/string_view.h"

#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>

#include "src/core/lib/gprpp/notification.h"
#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "test/core/memory_usage/memstats.h"
#include "test/core/util/test_config.h"

ABSL_FLAG(std::string, target, "", "Target host:port");
ABSL_FLAG(bool, secure, false, "Use SSL Credentials");
ABSL_FLAG(int, server_pid, 99999, "Server's pid");
ABSL_FLAG(int, size, 50, "Number of channels");

std::shared_ptr<grpc::Channel> CreateChannelForTest(int index) {
  // Set the authentication mechanism.
  std::shared_ptr<grpc::ChannelCredentials> creds =
      grpc::InsecureChannelCredentials();
  if (absl::GetFlag(FLAGS_secure)) {
    // TODO (chennancy) Add in secure credentials
    gpr_log(GPR_INFO, "Supposed to be secure, is not yet");
  }

  // Channel args to prevent connection from closing after RPC is done
  grpc::ChannelArguments channel_args;
  channel_args.SetInt(GRPC_ARG_MAX_CONNECTION_IDLE_MS, INT_MAX);
  channel_args.SetInt(GRPC_ARG_MAX_CONNECTION_AGE_MS, INT_MAX);
  // Arg to bypass mechanism that combines channels on the serverside if they
  // have the same channel args. Allows for one channel per connection
  channel_args.SetInt("grpc.memory_usage_counter", index);

  // Create a channel to the server and a stub
  std::shared_ptr<grpc::Channel> channel =
      CreateCustomChannel(absl::GetFlag(FLAGS_target), creds, channel_args);
  return channel;
}

struct CallParams {
  grpc::ClientContext context;
  grpc::testing::SimpleRequest request;
  grpc::testing::SimpleResponse response;
  grpc::testing::MemorySize snapshot_response;
  grpc_core::Notification done;
};

// Simple Unary RPC to send to confirm connection is open
std::shared_ptr<CallParams> UnaryCall(std::shared_ptr<grpc::Channel> channel) {
  std::unique_ptr<grpc::testing::BenchmarkService::Stub> stub =
      grpc::testing::BenchmarkService::NewStub(channel);

  // Start a call.
  auto params = std::make_shared<CallParams>();
  stub->async()->UnaryCall(&params->context, &params->request,
                           &params->response,
                           [params](const grpc::Status& status) {
                             if (!status.ok()) {
                               gpr_log(GPR_ERROR, "UnaryCall RPC failed.");
                             }
                             params->done.Notify();
                           });
  return params;
}

// Get memory usage of server's process before the server is made
std::shared_ptr<CallParams> GetBeforeSnapshot(
    std::shared_ptr<grpc::Channel> channel, long& before_server_memory) {
  std::unique_ptr<grpc::testing::BenchmarkService::Stub> stub =
      grpc::testing::BenchmarkService::NewStub(channel);

  // Start a call.
  auto params = std::make_shared<CallParams>();
  stub->async()->GetBeforeSnapshot(
      &params->context, &params->request, &params->snapshot_response,
      [params, &before_server_memory](const grpc::Status& status) {
        if (status.ok()) {
          before_server_memory = params->snapshot_response.rss();
          gpr_log(GPR_INFO, "Server Before RPC: %ld",
                  params->snapshot_response.rss());
          gpr_log(GPR_INFO, "GetBeforeSnapshot succeeded.");
        } else {
          gpr_log(GPR_ERROR, "GetBeforeSnapshot failed.");
        }
        params->done.Notify();
      });
  return params;
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
  gpr_log(GPR_INFO, "Client Size: %d", absl::GetFlag(FLAGS_size));

  // Getting initial memory usage
  std::shared_ptr<grpc::Channel> get_memory_channel = CreateChannelForTest(0);
  long before_server_memory;
  GetBeforeSnapshot(get_memory_channel, before_server_memory)
      ->done.WaitForNotification();
  long before_client_memory = GetMemUsage();

  // Create the channels and send an RPC to confirm they're open
  int size = absl::GetFlag(FLAGS_size);
  std::vector<std::shared_ptr<grpc::Channel>> channels_list(size);
  for (int i = 0; i < size; ++i) {
    std::shared_ptr<grpc::Channel> channel = CreateChannelForTest(i);
    channels_list[i] = channel;
    UnaryCall(channel)->done.WaitForNotification();
  }

  // Getting peak memory usage
  long peak_server_memory = GetMemUsage(absl::GetFlag(FLAGS_server_pid));
  long peak_client_memory = GetMemUsage();

  // Checking that all channels are still open
  for (int i = 0; i < size; ++i) {
    GPR_ASSERT(!std::exchange(channels_list[i], nullptr)
                    ->WaitForStateChange(GRPC_CHANNEL_READY,
                                         std::chrono::system_clock::now() +
                                             std::chrono::milliseconds(1)));
  }

  printf("---------Client channel stats--------\n");
  printf("client channel memory usage: %f bytes per channel\n",
         static_cast<double>(peak_client_memory - before_client_memory) / size *
             1024);
  printf("---------Server channel stats--------\n");
  printf("server channel memory usage: %f bytes per channel\n",
         static_cast<double>(peak_server_memory - before_server_memory) / size *
             1024);
  gpr_log(GPR_INFO, "Client Done");
  return 0;
}
