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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc++/grpc++.h>
#include <grpcpp/ext/gcp_observability.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {
namespace {

TEST(GcpObservabilityTest, Basic) {
  auto observability = grpc::GcpObservability::Init();
  EXPECT_EQ(observability.status(),
            absl::FailedPreconditionError(
                "Environment variables GRPC_GCP_OBSERVABILITY_CONFIG_FILE or "
                "GRPC_GCP_OBSERVABILITY_CONFIG "
                "not defined"));
  grpc_core::CoreConfiguration::Reset();
}

TEST(GcpObservabilityTest, ContinuesWorkingAfterFailure) {
  auto observability = grpc::GcpObservability::Init();
  EXPECT_FALSE(observability.ok());

  // Set up a synchronous server on a different thread to avoid the asynch
  // interface.
  grpc::ServerBuilder builder;
  TestServiceImpl service;
  int port = grpc_pick_unused_port_or_die();
  auto server_address = absl::StrCat("localhost:", port);
  // Use IPv4 here because it's less flaky than IPv6 ("[::]:0") on Travis.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(),
                           &port);
  builder.RegisterService(&service);
  auto server = builder.BuildAndStart();
  ASSERT_NE(nullptr, server);
  auto server_thread = std::thread([&]() { server->Wait(); });
  // Send a single RPC to make sure that things work.
  auto stub = EchoTestService::NewStub(
      grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));
  EchoRequest request;
  request.set_message("foo");
  EchoResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.message(), "foo");
  server->Shutdown();
  server_thread.join();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
