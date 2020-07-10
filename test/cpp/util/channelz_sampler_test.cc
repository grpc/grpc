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

#include <gtest/gtest.h>

// Test variables
std::string server_address("0.0.0.0:10000");
std::string custom_credentials_type("INSECURE_CREDENTIALS");

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
void RunServer() {
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
  std::cout << "Server listening on " << server_address << std::endl;

  server->Wait();
}

// Creata an echo client - set timeout as 0.15s
class EchoClientImpl {
 public:
  EchoClientImpl(std::shared_ptr<Channel> channel)
      : stub_(grpc::testing::TestService::NewStub(channel)) {}
  Status EmptyCall() {
    grpc::testing::Empty request;
    grpc::testing::Empty response;
    ClientContext context;
    int64_t timeout_microseconds = 150;
    context.set_deadline(
        grpc_timeout_milliseconds_to_deadline(timeout_microseconds));
    Status status = stub_->EmptyCall(&context, request, &response);
    return status;
  }

 private:
  std::unique_ptr<grpc::testing::TestService::Stub> stub_;
};

// Run client in a thread
void RunClient(std::string client_id) {
  // std::string target_str = "localhost:10000";
  grpc::ChannelArguments channel_args;
  std::shared_ptr<grpc::ChannelCredentials> channel_creds =
      grpc::testing::GetCredentialsProvider()->GetChannelCredentials(
          custom_credentials_type, &channel_args);
  EchoClientImpl echoer(grpc::CreateChannel(server_address, channel_creds));
  unsigned int client_echo_sleep_second = 1;

  std::cout << "Client " << client_id << " is echoing!" << std::endl;
  while (true) {
    Status status = echoer.EmptyCall();
    sleep(client_echo_sleep_second);
  }
}

// Test the channelz sampler
TEST(ChannelzSamplerTest, SimpleTest) {
  // server thread
  std::thread server_thread(RunServer);
  std::cout << "Wait 3 seconds to make sure server is started..." << std::endl;
  float server_start_seconds = 3.0;
  sleep(server_start_seconds);

  // client thread
  std::thread client_thread_1(RunClient, "1");
  std::thread client_thread_2(RunClient, "2");
  float run_services_seconds = 5.0;
  std::cout << "Run echo service for " << run_services_seconds << " seconds."
            << std::endl;
  sleep(run_services_seconds);

  // Run the channelz sampler
  std::string channelz_sampler_bin_path =
      "./bazel-bin/test/cpp/util/channelz_sampler";
  grpc::SubProcess* test_driver = new grpc::SubProcess(
      {std::move(channelz_sampler_bin_path),
       "--server_address=" + server_address,
       "--custom_credentials_type=" + custom_credentials_type});
  while (true)
    ;
  EXPECT_TRUE(true);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
