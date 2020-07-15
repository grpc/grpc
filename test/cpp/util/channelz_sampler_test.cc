/*
 *
 * Copyright 2016 gRPC authors.
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
#include <grpc/support/port_platform.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/ext/channelz_service_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <gtest/gtest.h>

#include <stdlib.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "include/grpcpp/ext/proto_server_reflection_plugin_impl.h"
#include "src/cpp/server/channelz/channelz_service.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/subprocess.h"
#include "test/cpp/util/test_credentials_provider.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

// Test variables
std::string server_address("0.0.0.0:10000");
std::string custom_credentials_type("INSECURE_CREDENTIALS");
std::string sampling_times = "2";
std::string sampling_interval_seconds = "5";
std::string output_json("./output.json");

// Creata an echo server - randomly delay 0.1 to 0.2 s
class EchoServerImpl final : public grpc::testing::TestService::Service {
  Status EmptyCall(::grpc::ServerContext* context,
                   const grpc::testing::Empty* request,
                   grpc::testing::Empty* response) {
    srand(unsigned(time(0)));
    unsigned int server_delay_microseconds = 100000;
    server_delay_microseconds += rand() % server_delay_microseconds;
    usleep(server_delay_microseconds);
    return Status::OK;
  }
};

// Run server in a thread
void RunServer(gpr_event* done_ev) {
  // register channelz service
  ::grpc::channelz::experimental::InitChannelzService();

  EchoServerImpl service;
  grpc::EnableDefaultHealthCheckService(true);
  grpc_impl::reflection::InitProtoReflectionServerBuilderPlugin();
  grpc::ServerBuilder builder;
  auto server_creds =
      grpc::testing::GetCredentialsProvider()->GetServerCredentials(
          custom_credentials_type);
  builder.AddListeningPort(server_address, server_creds);

  // forces channelz and channel tracing to be enabled.
  builder.AddChannelArgument(GRPC_ARG_ENABLE_CHANNELZ, 1);
  builder.AddChannelArgument(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE,
                             1024);
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  gpr_log(GPR_INFO, "Server listening on %s", server_address.c_str());
  while (true) {
    if (gpr_event_get(done_ev)) {
      return;
    }
  }
}

// Run client in a thread - - set timeout as 0.15s
void RunClient(std::string client_id, gpr_event* done_ev) {
  grpc::ChannelArguments channel_args;
  std::shared_ptr<grpc::ChannelCredentials> channel_creds =
      grpc::testing::GetCredentialsProvider()->GetChannelCredentials(
          custom_credentials_type, &channel_args);
  std::unique_ptr<grpc::testing::TestService::Stub> stub =
      grpc::testing::TestService::NewStub(
          grpc::CreateChannel(server_address, channel_creds));
  unsigned int client_echo_sleep_second = 1;

  gpr_log(GPR_INFO, "Client %s is echoing!", client_id.c_str());
  while (true) {
    if (gpr_event_get(done_ev)) {
      return;
    }
    sleep(client_echo_sleep_second);
    // Rcho RPC
    grpc::testing::Empty request;
    grpc::testing::Empty response;
    ClientContext context;
    int64_t timeout_microseconds = 150;
    context.set_deadline(
        grpc_timeout_milliseconds_to_deadline(timeout_microseconds));
    stub->EmptyCall(&context, request, &response);
  }
}

// Create the channelz to test the connection to the server
void WaitForConnection() {
  grpc::ChannelArguments channel_args;
  std::shared_ptr<grpc::ChannelCredentials> channel_creds =
      grpc::testing::GetCredentialsProvider()->GetChannelCredentials(
          custom_credentials_type, &channel_args);
  auto channel = grpc::CreateChannel(server_address, channel_creds);
  channel->WaitForConnected(grpc_timeout_seconds_to_deadline(3));
}

// Test the channelz sampler
TEST(ChannelzSamplerTest, SimpleTest) {
  // server thread
  gpr_event done_ev0;
  gpr_event_init(&done_ev0);
  std::thread server_thread(RunServer, &done_ev0);
  WaitForConnection();

  // client threads
  gpr_event done_ev1, done_ev2;
  gpr_event_init(&done_ev1);
  gpr_event_init(&done_ev2);
  std::thread client_thread_1(RunClient, "1", &done_ev1);
  std::thread client_thread_2(RunClient, "2", &done_ev2);

  // Run the channelz sampler
  std::string channelz_sampler_bin_path =
      "./bazel-bin/test/cpp/util/channelz_sampler";
  grpc::SubProcess* test_driver = new grpc::SubProcess(
      {std::move(channelz_sampler_bin_path),
       "--server_address=" + server_address,
       "--custom_credentials_type=" + custom_credentials_type,
       "--sampling_times=" + sampling_times,
       "--sampling_interval_seconds=" + sampling_interval_seconds,
       "--output_json=" + output_json});

  test_driver->Join();
  gpr_event_set(&done_ev1, (void*)1);
  gpr_event_set(&done_ev2, (void*)1);
  client_thread_1.join();
  client_thread_2.join();
  gpr_event_set(&done_ev0, (void*)1);
  server_thread.join();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
