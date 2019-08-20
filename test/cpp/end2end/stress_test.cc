/*
 *
 * Copyright 2015 gRPC authors.
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

#include <thread>

#include <grpc/grpc.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include <gtest/gtest.h>

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

const int kNumThreads = 100;  // Number of threads
const int kNumRpcs = 500;  // Number of RPCs per thread

namespace grpc {
namespace testing {

class End2endTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServerBuilder builder;
    int port = grpc_pick_unused_port_or_die();
    std::ostringstream server_address;
    server_address << "localhost:" << port;
    // Setup server
    builder.AddListeningPort(server_address.str(),
                              InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();

    // Create channel and stub
    std::shared_ptr<Channel> channel = grpc::CreateChannel(
        server_address.str(), InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }

  grpc::testing::EchoTestService::Stub* GetStub() { return stub_.get(); }

 private:
  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
};

static void SendRpc(grpc::testing::EchoTestService::Stub* stub, int num_rpcs,
                    int thread_num) {
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  for (int i = 0; i < num_rpcs; ++i) {
    ClientContext context;
    context.AddMetadata("thread_num", grpc::to_string(thread_num));
    context.AddMetadata("rpc_num", grpc::to_string(i));
    gpr_log(GPR_ERROR, "Thread %d sending rpc %d", thread_num, i);
    Status s = stub->Echo(&context, request, &response);
    gpr_log(GPR_ERROR, "Thread %d sent rpc %d ok %d", thread_num, i, s.ok());
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(response.message(), request.message());
  }
}

TEST_F(End2endTest, ThreadStress) {
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back(SendRpc, GetStub(), kNumRpcs, i);
  }
  for (int i = 0; i < kNumThreads; ++i) {
    threads[i].join();
  }
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
