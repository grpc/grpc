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

#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "absl/memory/memory.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/security/binder_credentials.h>
#include <grpcpp/security/binder_security_policy.h>

#include "src/core/ext/transport/binder/client/channel_create_impl.h"
#include "test/core/transport/binder/end2end/fake_binder.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {

namespace {

class BinderServerCredentialsImpl final : public ServerCredentials {
 public:
  int AddPortToServer(const std::string& addr, grpc_server* server) override {
    return grpc_core::AddBinderPort(
        addr, server,
        [](grpc_binder::TransactionReceiver::OnTransactCb transact_cb) {
          return std::make_unique<
              grpc_binder::end2end_testing::FakeTransactionReceiver>(
              nullptr, std::move(transact_cb));
        },
        std::make_shared<
            grpc::experimental::binder::UntrustedSecurityPolicy>());
  }

  void SetAuthMetadataProcessor(
      const std::shared_ptr<AuthMetadataProcessor>& /*processor*/) override {
    grpc_core::Crash("unreachable");
  }

 private:
  bool IsInsecure() const override { return true; }
};

}  // namespace

std::shared_ptr<ServerCredentials> BinderServerCredentials() {
  return std::shared_ptr<ServerCredentials>(new BinderServerCredentialsImpl());
}

std::shared_ptr<grpc::Channel> CreateBinderChannel(
    std::unique_ptr<grpc_binder::Binder> endpoint_binder) {
  return grpc::CreateChannelInternal(
      "",
      grpc::internal::CreateDirectBinderChannelImplForTesting(
          std::move(endpoint_binder), nullptr,
          std::make_shared<
              grpc::experimental::binder::UntrustedSecurityPolicy>()),
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
  static void SetUpTestSuite() { grpc_init(); }
  static void TearDownTestSuite() { grpc_shutdown(); }
};

#ifndef GPR_SUPPORT_BINDER_TRANSPORT
TEST(BinderServerCredentialsTest,
     FailedInEnvironmentsNotSupportingBinderTransport) {
  grpc::ServerBuilder server_builder;
  grpc::testing::TestServiceImpl service;
  server_builder.RegisterService(&service);
  server_builder.AddListeningPort(
      "binder:fail",
      grpc::experimental::BinderServerCredentials(
          std::make_shared<
              grpc::experimental::binder::UntrustedSecurityPolicy>()));
  EXPECT_EQ(server_builder.BuildAndStart(), nullptr);
}
#endif  // !GPR_SUPPORT_BINDER_TRANSPORT

TEST_F(BinderServerTest, BuildAndStart) {
  grpc::ServerBuilder server_builder;
  grpc::testing::TestServiceImpl service;
  server_builder.RegisterService(&service);
  server_builder.AddListeningPort("binder:example.service",
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
  grpc::testing::TestServiceImpl service;
  server_builder.RegisterService(&service);
  // Error: binder address should begin with binder:
  server_builder.AddListeningPort("localhost:12345",
                                  grpc::testing::BinderServerCredentials());
  std::unique_ptr<grpc::Server> server = server_builder.BuildAndStart();
  EXPECT_EQ(server, nullptr);
}

TEST_F(BinderServerTest, CreateChannelWithEndpointBinder) {
  grpc::ServerBuilder server_builder;
  grpc::testing::TestServiceImpl service;
  server_builder.RegisterService(&service);
  server_builder.AddListeningPort("binder:example.service",
                                  grpc::testing::BinderServerCredentials());
  std::unique_ptr<grpc::Server> server = server_builder.BuildAndStart();
  void* raw_endpoint_binder =
      grpc::experimental::binder::GetEndpointBinder("example.service");
  std::unique_ptr<grpc_binder::Binder> endpoint_binder =
      std::make_unique<grpc_binder::end2end_testing::FakeBinder>(
          static_cast<grpc_binder::end2end_testing::FakeEndpoint*>(
              raw_endpoint_binder));
  std::shared_ptr<grpc::Channel> channel =
      grpc::testing::CreateBinderChannel(std::move(endpoint_binder));
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);
  grpc::testing::EchoRequest request;
  grpc::testing::EchoResponse response;
  grpc::ClientContext context;
  request.set_message("BinderServerBuilder");
  grpc::Status status = stub->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.message(), "BinderServerBuilder");
  server->Shutdown();
}

TEST_F(BinderServerTest, CreateChannelWithEndpointBinderMultipleConnections) {
  grpc::ServerBuilder server_builder;
  grpc::testing::TestServiceImpl service;
  server_builder.RegisterService(&service);
  server_builder.AddListeningPort("binder:example.service.multiple.connections",
                                  grpc::testing::BinderServerCredentials());
  std::unique_ptr<grpc::Server> server = server_builder.BuildAndStart();
  void* raw_endpoint_binder = grpc::experimental::binder::GetEndpointBinder(
      "example.service.multiple.connections");
  constexpr size_t kNumThreads = 10;

  auto thread_fn = [&](size_t id) {
    std::unique_ptr<grpc_binder::Binder> endpoint_binder =
        std::make_unique<grpc_binder::end2end_testing::FakeBinder>(
            static_cast<grpc_binder::end2end_testing::FakeEndpoint*>(
                raw_endpoint_binder));
    std::shared_ptr<grpc::Channel> channel =
        grpc::testing::CreateBinderChannel(std::move(endpoint_binder));
    std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
        grpc::testing::EchoTestService::NewStub(channel);
    grpc::testing::EchoRequest request;
    grpc::testing::EchoResponse response;
    grpc::ClientContext context;
    request.set_message(absl::StrFormat("BinderServerBuilder-%d", id));
    grpc::Status status = stub->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.message(),
              absl::StrFormat("BinderServerBuilder-%d", id));
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

TEST_F(BinderServerTest, CreateChannelWithEndpointBinderParallelRequests) {
  grpc::ServerBuilder server_builder;
  grpc::testing::TestServiceImpl service;
  server_builder.RegisterService(&service);
  server_builder.AddListeningPort("binder:example.service",
                                  grpc::testing::BinderServerCredentials());
  std::unique_ptr<grpc::Server> server = server_builder.BuildAndStart();
  void* raw_endpoint_binder =
      grpc::experimental::binder::GetEndpointBinder("example.service");
  std::unique_ptr<grpc_binder::Binder> endpoint_binder =
      std::make_unique<grpc_binder::end2end_testing::FakeBinder>(
          static_cast<grpc_binder::end2end_testing::FakeEndpoint*>(
              raw_endpoint_binder));
  std::shared_ptr<grpc::Channel> channel =
      grpc::testing::CreateBinderChannel(std::move(endpoint_binder));
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub =
      grpc::testing::EchoTestService::NewStub(channel);

  constexpr size_t kNumRequests = 10;

  auto thread_fn = [&](size_t id) {
    grpc::testing::EchoRequest request;
    std::string msg = absl::StrFormat("BinderServerBuilder-%d", id);
    request.set_message(msg);
    grpc::testing::EchoResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub->Echo(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.message(), msg);
  };
  std::vector<std::thread> threads(kNumRequests);
  for (size_t i = 0; i < kNumRequests; ++i) {
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
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
