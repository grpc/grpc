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

#include <grpc/impl/channel_arg_names.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>
#include <limits.h>
#include <stdio.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "src/core/util/notification.h"
#include "src/cpp/ext/chaotic_good.h"
#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "test/core/memory_usage/memstats.h"
#include "test/core/test_util/test_config.h"

ABSL_FLAG(std::string, target, "", "Target host:port");
ABSL_FLAG(bool, secure, false, "Use SSL Credentials");
ABSL_FLAG(int, server_pid, 0, "Server's pid");
ABSL_FLAG(int, size, 50, "Number of channels");
ABSL_FLAG(bool, chaotic_good, false, "Use chaotic good");

std::shared_ptr<grpc::Channel> CreateChannelForTest(int index) {
  // Set the authentication mechanism.
  std::shared_ptr<grpc::ChannelCredentials> creds =
      grpc::InsecureChannelCredentials();
  if (absl::GetFlag(FLAGS_chaotic_good)) {
    creds = grpc::ChaoticGoodInsecureChannelCredentials();
  } else if (absl::GetFlag(FLAGS_secure)) {
    // TODO (chennancy) Add in secure credentials
    LOG(INFO) << "Supposed to be secure, is not yet";
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
                               LOG(ERROR) << "UnaryCall RPC failed.";
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
          LOG(INFO) << "Server Before RPC: " << params->snapshot_response.rss();
          LOG(INFO) << "GetBeforeSnapshot succeeded.";
        } else {
          LOG(ERROR) << "GetBeforeSnapshot failed.";
        }
        params->done.Notify();
      });
  return params;
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  char* fake_argv[1];
  CHECK_GE(argc, 1);
  fake_argv[0] = argv[0];
  grpc::testing::TestEnvironment env(&argc, argv);
  if (absl::GetFlag(FLAGS_target).empty()) {
    LOG(ERROR) << "Client: No target port entered";
    return 1;
  }
  LOG(INFO) << "Client Target: " << absl::GetFlag(FLAGS_target);
  LOG(INFO) << "Client Size: " << absl::GetFlag(FLAGS_size);

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
  long peak_server_memory = absl::GetFlag(FLAGS_server_pid) > 0
                                ? GetMemUsage(absl::GetFlag(FLAGS_server_pid))
                                : 0;
  long peak_client_memory = GetMemUsage();

  // Checking that all channels are still open
  for (int i = 0; i < size; ++i) {
    CHECK(!std::exchange(channels_list[i], nullptr)
               ->WaitForStateChange(GRPC_CHANNEL_READY,
                                    std::chrono::system_clock::now() +
                                        std::chrono::milliseconds(1)));
  }

  std::string prefix;
  if (absl::StartsWith(absl::GetFlag(FLAGS_target), "xds:")) prefix = "xds ";
  if (absl::GetFlag(FLAGS_server_pid) == 0) {
    absl::StrAppend(&prefix, "multi_address ");
  }
  printf("---------Client channel stats--------\n");
  printf("%sclient channel memory usage: %f bytes per channel\n",
         prefix.c_str(),
         static_cast<double>(peak_client_memory - before_client_memory) / size *
             1024);
  if (absl::GetFlag(FLAGS_server_pid) > 0) {
    printf("---------Server channel stats--------\n");
    printf("%sserver channel memory usage: %f bytes per channel\n",
           prefix.c_str(),
           static_cast<double>(peak_server_memory - before_server_memory) /
               size * 1024);
  }
  LOG(INFO) << "Client Done";
  return 0;
}
