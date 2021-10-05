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
#include "test/core/util/tls_utils.h"
#include "test/cpp/end2end/test_service_impl.h"

namespace grpc {
namespace testing {
namespace {

constexpr char kMessage[] = "Hello";

class SdkAuthzEnd2EndTest : public ::testing::Test {
 protected:
  SdkAuthzEnd2EndTest()
      : server_address_(
            absl::StrCat("localhost:", grpc_pick_unused_port_or_die())),
        server_creds_(
            std::shared_ptr<ServerCredentials>(new SecureServerCredentials(
                grpc_fake_transport_security_server_credentials_create()))),
        channel_creds_(
            std::shared_ptr<ChannelCredentials>(new SecureChannelCredentials(
                grpc_fake_transport_security_credentials_create()))) {}

  ~SdkAuthzEnd2EndTest() override { server_->Shutdown(); }

  // Replaces existing credentials with insecure credentials.
  void UseInsecureCredentials() {
    server_creds_ = InsecureServerCredentials();
    channel_creds_ = InsecureChannelCredentials();
  }

  // Creates server with sdk authorization enabled when provider is not null.
  void InitServer(
      std::shared_ptr<experimental::AuthorizationPolicyProviderInterface>
          provider) {
    ServerBuilder builder;
    builder.AddListeningPort(server_address_, std::move(server_creds_));
    builder.experimental().SetAuthorizationPolicyProvider(std::move(provider));
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  std::shared_ptr<experimental::AuthorizationPolicyProviderInterface>
  CreateStaticAuthzPolicyProvider(const std::string& policy) {
    grpc::Status status;
    auto provider = experimental::StaticDataAuthorizationPolicyProvider::Create(
        policy, &status);
    EXPECT_TRUE(status.ok());
    return provider;
  }

  std::shared_ptr<experimental::AuthorizationPolicyProviderInterface>
  CreateFileWatcherAuthzPolicyProvider(const std::string& policy_path,
                                       unsigned int refresh_interval_sec) {
    grpc::Status status;
    auto provider =
        experimental::FileWatcherAuthorizationPolicyProvider::Create(
            policy_path, refresh_interval_sec, &status);
    EXPECT_TRUE(status.ok());
    return provider;
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

TEST_F(SdkAuthzEnd2EndTest,
       StaticInitAllowsRpcRequestNoMatchInDenyMatchInAllow) {
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
      "            \"key\": \"key-foo\","
      "            \"values\": [\"foo1\", \"foo2\"]"
      "          },"
      "          {"
      "            \"key\": \"key-bar\","
      "            \"values\": [\"bar1\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_clientstreamingecho\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/ClientStreamingEcho\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  InitServer(CreateStaticAuthzPolicyProvider(policy));
  auto channel = BuildChannel();
  ClientContext context;
  context.AddMetadata("key-foo", "foo2");
  context.AddMetadata("key-bar", "bar1");
  context.AddMetadata("key-baz", "baz1");
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp.message(), kMessage);
}

TEST_F(SdkAuthzEnd2EndTest, StaticInitDeniesRpcRequestNoMatchInAllowAndDeny) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_bar\","
      "      \"source\": {"
      "        \"principals\": ["
      "          \"bar\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  InitServer(CreateStaticAuthzPolicyProvider(policy));
  auto channel = BuildChannel();
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp.message().empty());
}

TEST_F(SdkAuthzEnd2EndTest, StaticInitDeniesRpcRequestMatchInDenyMatchInAllow) {
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
  InitServer(CreateStaticAuthzPolicyProvider(policy));
  auto channel = BuildChannel();
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp.message().empty());
}

TEST_F(SdkAuthzEnd2EndTest,
       StaticInitDeniesRpcRequestMatchInDenyNoMatchInAllow) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_clientstreamingecho\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/ClientStreamingEcho\""
      "        ]"
      "      }"
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
  InitServer(CreateStaticAuthzPolicyProvider(policy));
  auto channel = BuildChannel();
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp.message().empty());
}

TEST_F(SdkAuthzEnd2EndTest, StaticInitAllowsRpcRequestEmptyDenyMatchInAllow) {
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
      "            \"key\": \"key-foo\","
      "            \"values\": [\"foo1\", \"foo2\"]"
      "          },"
      "          {"
      "            \"key\": \"key-bar\","
      "            \"values\": [\"bar1\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  InitServer(CreateStaticAuthzPolicyProvider(policy));
  auto channel = BuildChannel();
  ClientContext context;
  context.AddMetadata("key-foo", "foo2");
  context.AddMetadata("key-bar", "bar1");
  context.AddMetadata("key-baz", "baz1");
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp.message(), kMessage);
}

TEST_F(SdkAuthzEnd2EndTest, StaticInitDeniesRpcRequestEmptyDenyNoMatchInAllow) {
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
      "            \"key\": \"key-foo\","
      "            \"values\": [\"foo1\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  InitServer(CreateStaticAuthzPolicyProvider(policy));
  auto channel = BuildChannel();
  ClientContext context;
  context.AddMetadata("key-bar", "bar1");
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp.message().empty());
}

TEST_F(
    SdkAuthzEnd2EndTest,
    StaticInitDeniesRpcRequestWithPrincipalsFieldOnUnauthenticatedConnection) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_echo\","
      "      \"source\": {"
      "        \"principals\": ["
      "          \"foo\""
      "        ]"
      "      },"
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/Echo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  UseInsecureCredentials();
  InitServer(CreateStaticAuthzPolicyProvider(policy));
  auto channel = BuildChannel();
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp.message().empty());
}

TEST_F(SdkAuthzEnd2EndTest,
       FileWatcherInitAllowsRpcRequestNoMatchInDenyMatchInAllow) {
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
      "            \"key\": \"key-foo\","
      "            \"values\": [\"foo1\", \"foo2\"]"
      "          },"
      "          {"
      "            \"key\": \"key-bar\","
      "            \"values\": [\"bar1\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_clientstreamingecho\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/ClientStreamingEcho\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(policy);
  InitServer(CreateFileWatcherAuthzPolicyProvider(tmp_policy.name(), 5));
  auto channel = BuildChannel();
  ClientContext context;
  context.AddMetadata("key-foo", "foo2");
  context.AddMetadata("key-bar", "bar1");
  context.AddMetadata("key-baz", "baz1");
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp.message(), kMessage);
}

TEST_F(SdkAuthzEnd2EndTest,
       FileWatcherInitDeniesRpcRequestNoMatchInAllowAndDeny) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_bar\","
      "      \"source\": {"
      "        \"principals\": ["
      "          \"bar\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(policy);
  InitServer(CreateFileWatcherAuthzPolicyProvider(tmp_policy.name(), 5));
  auto channel = BuildChannel();
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp.message().empty());
}

TEST_F(SdkAuthzEnd2EndTest,
       FileWatcherInitDeniesRpcRequestMatchInDenyMatchInAllow) {
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
  grpc_core::testing::TmpFile tmp_policy(policy);
  InitServer(CreateFileWatcherAuthzPolicyProvider(tmp_policy.name(), 5));
  auto channel = BuildChannel();
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp.message().empty());
}

TEST_F(SdkAuthzEnd2EndTest,
       FileWatcherInitDeniesRpcRequestMatchInDenyNoMatchInAllow) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_clientstreamingecho\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/ClientStreamingEcho\""
      "        ]"
      "      }"
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
  grpc_core::testing::TmpFile tmp_policy(policy);
  InitServer(CreateFileWatcherAuthzPolicyProvider(tmp_policy.name(), 5));
  auto channel = BuildChannel();
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp.message().empty());
}

TEST_F(SdkAuthzEnd2EndTest,
       FileWatcherInitAllowsRpcRequestEmptyDenyMatchInAllow) {
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
      "            \"key\": \"key-foo\","
      "            \"values\": [\"foo1\", \"foo2\"]"
      "          },"
      "          {"
      "            \"key\": \"key-bar\","
      "            \"values\": [\"bar1\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(policy);
  InitServer(CreateFileWatcherAuthzPolicyProvider(tmp_policy.name(), 5));
  auto channel = BuildChannel();
  ClientContext context;
  context.AddMetadata("key-foo", "foo2");
  context.AddMetadata("key-bar", "bar1");
  context.AddMetadata("key-baz", "baz1");
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp.message(), kMessage);
}

TEST_F(SdkAuthzEnd2EndTest,
       FileWatcherInitDeniesRpcRequestEmptyDenyNoMatchInAllow) {
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
      "            \"key\": \"key-foo\","
      "            \"values\": [\"foo1\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(policy);
  InitServer(CreateFileWatcherAuthzPolicyProvider(tmp_policy.name(), 5));
  auto channel = BuildChannel();
  ClientContext context;
  context.AddMetadata("key-bar", "bar1");
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp.message().empty());
}

TEST_F(SdkAuthzEnd2EndTest, FileWatcherValidPolicyRefresh) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_echo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/Echo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(policy);
  InitServer(CreateFileWatcherAuthzPolicyProvider(tmp_policy.name(), 1));
  auto channel = BuildChannel();
  ClientContext context1;
  grpc::testing::EchoResponse resp1;
  grpc::Status status = SendRpc(channel, &context1, &resp1);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp1.message(), kMessage);
  // Replace the existing policy with a new authorization policy.
  policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
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
  tmp_policy.RewriteFile(policy);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  ClientContext context2;
  grpc::testing::EchoResponse resp2;
  status = SendRpc(channel, &context2, &resp2);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp2.message().empty());
}

TEST_F(SdkAuthzEnd2EndTest, FileWatcherInvalidPolicyRefreshSkipsReload) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_echo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/Echo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(policy);
  InitServer(CreateFileWatcherAuthzPolicyProvider(tmp_policy.name(), 1));
  auto channel = BuildChannel();
  ClientContext context1;
  grpc::testing::EchoResponse resp1;
  grpc::Status status = SendRpc(channel, &context1, &resp1);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp1.message(), kMessage);
  // Replaces existing policy with an invalid authorization policy.
  policy = "{}";
  tmp_policy.RewriteFile(policy);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  ClientContext context2;
  grpc::testing::EchoResponse resp2;
  status = SendRpc(channel, &context2, &resp2);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp2.message(), kMessage);
}

TEST_F(SdkAuthzEnd2EndTest, FileWatcherRecoversFromFailure) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_echo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/Echo\""
      "        ]"
      "      }"
      "    }"
      "  ]"
      "}";
  grpc_core::testing::TmpFile tmp_policy(policy);
  InitServer(CreateFileWatcherAuthzPolicyProvider(tmp_policy.name(), 1));
  auto channel = BuildChannel();
  ClientContext context1;
  grpc::testing::EchoResponse resp1;
  grpc::Status status = SendRpc(channel, &context1, &resp1);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp1.message(), kMessage);
  // Replaces existing policy with an invalid authorization policy.
  policy = "{}";
  tmp_policy.RewriteFile(policy);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  ClientContext context2;
  grpc::testing::EchoResponse resp2;
  status = SendRpc(channel, &context2, &resp2);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp2.message(), kMessage);
  // Replace the existing invalid policy with a valid authorization policy.
  policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_foo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*/foo\""
      "        ]"
      "      }"
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
  tmp_policy.RewriteFile(policy);
  // Wait 2 seconds for the provider's refresh thread to read the updated files.
  gpr_sleep_until(grpc_timeout_seconds_to_deadline(2));
  ClientContext context3;
  grpc::testing::EchoResponse resp3;
  status = SendRpc(channel, &context3, &resp3);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp3.message().empty());
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
