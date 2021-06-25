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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/authorization_policy_provider.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/cpp/client/secure_credentials.h"
#include "src/cpp/server/secure_server_credentials.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {
namespace {

constexpr char kMessage[] = "Hello";

class SdkAuthzEnd2EndTest : public ::testing::Test {
 protected:
  SdkAuthzEnd2EndTest()
      : server_address_("localhost:" +
                        std::to_string(grpc_pick_unused_port_or_die())),
        server_creds_(
            std::shared_ptr<ServerCredentials>(new SecureServerCredentials(
                grpc_fake_transport_security_server_credentials_create()))),
        channel_creds_(
            std::shared_ptr<ChannelCredentials>(new SecureChannelCredentials(
                grpc_fake_transport_security_credentials_create()))) {}

  ~SdkAuthzEnd2EndTest() override { server_->Shutdown(); }

  // Creates server with sdk authorization enabled using static data policy
  // provider.
  void InitServerWithStaticAuthzProvider(const std::string& policy) {
    ServerBuilder builder;
    builder.AddListeningPort(server_address_, std::move(server_creds_));
    grpc::Status status;
    auto provider = experimental::StaticDataAuthorizationPolicyProvider::Create(
        policy, &status);
    ASSERT_TRUE(status.ok());
    builder.experimental().SetAuthorizationPolicyProvider(std::move(provider));
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  void InitServerWithoutAuthorization() {
    ServerBuilder builder;
    builder.AddListeningPort(server_address_, std::move(server_creds_));
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  std::shared_ptr<Channel> BuildChannel() {
    ChannelArguments args;
    return ::grpc::CreateCustomChannel(server_address_, channel_creds_, args);
  }

  grpc::Status SendRpc(const std::shared_ptr<Channel>& channel,
                       ClientContext* context,
                       grpc::testing::EchoResponse* response = nullptr) {
    auto stub = grpc::testing::EchoTestService::NewStub(channel);
    grpc::testing::EchoRequest request;
    request.set_message(kMessage);
    return stub->Echo(context, request, response);
  }

  std::string server_address_;
  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
  std::shared_ptr<ServerCredentials> server_creds_;
  std::shared_ptr<ChannelCredentials> channel_creds_;
};

TEST_F(SdkAuthzEnd2EndTest, StaticInitDeniesRpcRequestDenyListMatches) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_all\""
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_echo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/Echo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  InitServerWithStaticAuthzProvider(policy);
  auto channel = BuildChannel();
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp.message().empty());
}

TEST_F(SdkAuthzEnd2EndTest, StaticInitAllowsRpcRequest) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_echo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/Echo\""
      "        ],"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"foo\","
      "            \"values\": [\"val-foo1\", \"val-foo2\"]"
      "          },"
      "          {"
      "            \"key\": \"bar\","
      "            \"values\": [\"val-bar1\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  InitServerWithStaticAuthzProvider(policy);
  auto channel = BuildChannel();
  ClientContext context;
  context.AddMetadata("foo", "val-foo2");
  context.AddMetadata("bar", "val-bar1");
  context.AddMetadata("baz", "val-baz");
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp.message(), kMessage);
}

TEST_F(SdkAuthzEnd2EndTest, StaticInitDeniesRpcRequestNoMatchFound) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"policy_1\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/SomeMethod\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  InitServerWithStaticAuthzProvider(policy);
  auto channel = BuildChannel();
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp.message().empty());
}

TEST_F(SdkAuthzEnd2EndTest, AllowsRpcRequestSdkAuthorizationDisabled) {
  InitServerWithoutAuthorization();
  auto channel = BuildChannel();
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp.message(), kMessage);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  const auto result = RUN_ALL_TESTS();
  return result;
}
