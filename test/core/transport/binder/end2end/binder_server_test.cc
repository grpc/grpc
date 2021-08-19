// Copyright 2021 gRPC authors.
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

#include "src/core/ext/transport/binder/server/binder_server.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/grpc_library.h>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

#include "src/core/ext/transport/binder/client/channel_create_impl.h"
#include "src/core/ext/transport/binder/server/binder_server_credentials.h"
#include "test/core/transport/binder/end2end/echo_service.h"
#include "test/core/transport/binder/end2end/fake_binder.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {

std::shared_ptr<ServerCredentials> BinderServerCredentials() {
  return std::shared_ptr<ServerCredentials>(
      new grpc::internal::BinderServerCredentialsImpl<
          grpc_binder::end2end_testing::FakeTransactionReceiver>());
}

std::shared_ptr<grpc::Channel> CreateBinderChannel(
    std::unique_ptr<grpc_binder::Binder> endpoint_binder) {
  grpc::internal::GrpcLibrary init_lib;
  init_lib.init();

  return grpc::CreateChannelInternal(
      "",
      grpc::internal::CreateChannelFromBinderImpl(std::move(endpoint_binder),
                                                  nullptr),
      std::vector<std::unique_ptr<
          grpc::experimental::ClientInterceptorFactoryInterface>>());
}

}  // namespace testing
}  // namespace grpc

namespace {

class BinderServerTest : public ::testing::Test {
 public:
  BinderServerTest() {
    grpc_binder::end2end_testing::g_transaction_processor =
        new grpc_binder::end2end_testing::TransactionProcessor();
  }
  ~BinderServerTest() override {
    delete grpc_binder::end2end_testing::g_transaction_processor;
  }
  static void SetUpTestSuite() {
    grpc_init();
    grpc_endpoint_binder_pool_init();
  }
  static void TearDownTestSuite() {
    grpc_endpoint_binder_pool_shutdown();
    grpc_shutdown();
  }
};

TEST_F(BinderServerTest, BuildAndStart) {
  grpc::ServerBuilder server_builder;
  grpc_binder::end2end_testing::EchoServer service;
  server_builder.RegisterService(&service);
  server_builder.AddListeningPort("binder://example.service",
                                  grpc::testing::BinderServerCredentials());
  std::unique_ptr<grpc::Server> server = server_builder.BuildAndStart();
  EXPECT_NE(grpc::experimental::binder::GetEndpointBinder("example.service"),
            nullptr);
  server->Shutdown();
  EXPECT_EQ(grpc::experimental::binder::GetEndpointBinder("example.service"),
            nullptr);
}

TEST_F(BinderServerTest, BuildAndStartFailed) {
  grpc::ServerBuilder server_builder;
  grpc_binder::end2end_testing::EchoServer service;
  server_builder.RegisterService(&service);
  // Error: binder address should begin with binder:
  server_builder.AddListeningPort("localhost:12345",
                                  grpc::testing::BinderServerCredentials());
  std::unique_ptr<grpc::Server> server = server_builder.BuildAndStart();
  EXPECT_EQ(server, nullptr);
}

TEST_F(BinderServerTest, CreateChannelWithEndpointBinder) {
  grpc::ServerBuilder server_builder;
  grpc_binder::end2end_testing::EchoServer service;
  server_builder.RegisterService(&service);
  server_builder.AddListeningPort("binder://example.service",
                                  grpc::testing::BinderServerCredentials());
  std::unique_ptr<grpc::Server> server = server_builder.BuildAndStart();
  void* raw_endpoint_binder =
      grpc::experimental::binder::GetEndpointBinder("example.service");
  std::unique_ptr<grpc_binder::Binder> endpoint_binder =
      absl::make_unique<grpc_binder::end2end_testing::FakeBinder>(
          static_cast<grpc_binder::end2end_testing::FakeEndpoint*>(
              raw_endpoint_binder));
  std::shared_ptr<grpc::Channel> channel =
      grpc::testing::CreateBinderChannel(std::move(endpoint_binder));
  std::unique_ptr<grpc_binder::end2end_testing::EchoService::Stub> stub =
      grpc_binder::end2end_testing::EchoService::NewStub(channel);
  grpc_binder::end2end_testing::EchoRequest request;
  grpc_binder::end2end_testing::EchoResponse response;
  grpc::ClientContext context;
  request.set_text("BinderServerBuilder");
  grpc::Status status = stub->EchoUnaryCall(&context, request, &response);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.text(), "BinderServerBuilder");
  server->Shutdown();
}

TEST_F(BinderServerTest, CreateChannelWithEndpointBinderMultipleConnections) {
  grpc::ServerBuilder server_builder;
  grpc_binder::end2end_testing::EchoServer service;
  server_builder.RegisterService(&service);
  server_builder.AddListeningPort(
      "binder://example.service.multiple.connections",
      grpc::testing::BinderServerCredentials());
  std::unique_ptr<grpc::Server> server = server_builder.BuildAndStart();
  void* raw_endpoint_binder = grpc::experimental::binder::GetEndpointBinder(
      "example.service.multiple.connections");
  constexpr size_t kNumThreads = 128;

  auto thread_fn = [&](size_t id) {
    std::unique_ptr<grpc_binder::Binder> endpoint_binder =
        absl::make_unique<grpc_binder::end2end_testing::FakeBinder>(
            static_cast<grpc_binder::end2end_testing::FakeEndpoint*>(
                raw_endpoint_binder));
    std::shared_ptr<grpc::Channel> channel =
        grpc::testing::CreateBinderChannel(std::move(endpoint_binder));
    std::unique_ptr<grpc_binder::end2end_testing::EchoService::Stub> stub =
        grpc_binder::end2end_testing::EchoService::NewStub(channel);
    grpc_binder::end2end_testing::EchoRequest request;
    grpc_binder::end2end_testing::EchoResponse response;
    grpc::ClientContext context;
    request.set_text(absl::StrFormat("BinderServerBuilder-%d", id));
    grpc::Status status = stub->EchoUnaryCall(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.text(), absl::StrFormat("BinderServerBuilder-%d", id));
  };

  std::vector<std::thread> threads(kNumThreads);
  for (size_t i = 0; i < kNumThreads; ++i) {
    threads[i] = std::thread(thread_fn, i);
  }
  for (auto& thr : threads) {
    thr.join();
  }
  server->Shutdown();
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
}
