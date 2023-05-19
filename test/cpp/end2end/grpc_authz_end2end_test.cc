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

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/support/json.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/audit_logging.h>
#include <grpcpp/security/authorization_policy_provider.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/json/json_writer.h"
#include "src/core/lib/security/authorization/audit_logging.h"
#include "src/core/lib/security/authorization/grpc_authorization_policy_provider.h"
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

constexpr char kCaCertPath[] = "src/core/tsi/test_creds/ca.pem";
constexpr char kServerCertPath[] = "src/core/tsi/test_creds/server1.pem";
constexpr char kServerKeyPath[] = "src/core/tsi/test_creds/server1.key";
constexpr char kClientCertPath[] =
    "src/core/tsi/test_creds/client-with-spiffe.pem";
constexpr char kClientKeyPath[] =
    "src/core/tsi/test_creds/client-with-spiffe.key";

constexpr char kMessage[] = "Hello";

constexpr absl::string_view kLoggerName = "test_logger";

using experimental::AuditContext;
using experimental::AuditLogger;
using experimental::AuditLoggerFactory;
using experimental::RegisterAuditLoggerFactory;
using grpc_core::experimental::AuditLoggerRegistry;

std::string ReadFile(const char* file_path) {
  grpc_slice slice;
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("load_file", grpc_load_file(file_path, 0, &slice)));
  std::string file_contents(grpc_core::StringViewFromSlice(slice));
  grpc_slice_unref(slice);
  return file_contents;
}

class TestAuditLogger : public AuditLogger {
 public:
  explicit TestAuditLogger(int* log_count) : log_count_(log_count) {}

  absl::string_view name() const override { return kLoggerName; }
  void Log(const AuditContext&) override { *log_count_ += 1; }

 private:
  int* log_count_;
};

class TestAuditLoggerFactory : public AuditLoggerFactory {
 public:
  class Config : public AuditLoggerFactory::Config {
    absl::string_view name() const override { return kLoggerName; }
    std::string ToString() const override { return "{}"; }
  };

  explicit TestAuditLoggerFactory(int* log_count) : log_count_(log_count) {}

  absl::string_view name() const override { return kLoggerName; }
  absl::StatusOr<std::unique_ptr<AuditLoggerFactory::Config>>
  ParseAuditLoggerConfig(const grpc_core::experimental::Json&) override {
    return std::make_unique<Config>();
  }
  std::unique_ptr<AuditLogger> CreateAuditLogger(
      std::unique_ptr<AuditLoggerFactory::Config>) override {
    return std::make_unique<TestAuditLogger>(log_count_);
  }

 private:
  int* log_count_;
};

class GrpcAuthzEnd2EndTest : public ::testing::Test {
 protected:
  GrpcAuthzEnd2EndTest()
      : server_address_(
            absl::StrCat("localhost:", grpc_pick_unused_port_or_die())) {
    std::string root_cert = ReadFile(kCaCertPath);
    std::string identity_cert = ReadFile(kServerCertPath);
    std::string private_key = ReadFile(kServerKeyPath);
    std::vector<experimental::IdentityKeyCertPair>
        server_identity_key_cert_pairs = {{private_key, identity_cert}};
    grpc::experimental::TlsServerCredentialsOptions server_options(
        std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
            root_cert, server_identity_key_cert_pairs));
    server_options.watch_root_certs();
    server_options.watch_identity_key_cert_pairs();
    server_options.set_cert_request_type(
        GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY);
    server_creds_ = grpc::experimental::TlsServerCredentials(server_options);
    std::vector<experimental::IdentityKeyCertPair>
        channel_identity_key_cert_pairs = {
            {ReadFile(kClientKeyPath), ReadFile(kClientCertPath)}};
    grpc::experimental::TlsChannelCredentialsOptions channel_options;
    channel_options.set_certificate_provider(
        std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
            ReadFile(kCaCertPath), channel_identity_key_cert_pairs));
    channel_options.watch_identity_key_cert_pairs();
    channel_options.watch_root_certs();
    channel_creds_ = grpc::experimental::TlsCredentials(channel_options);

    RegisterAuditLoggerFactory(
        std::make_unique<TestAuditLoggerFactory>(&log_count_));
  }

  ~GrpcAuthzEnd2EndTest() override {
    AuditLoggerRegistry::TestOnlyResetRegistry();
    server_->Shutdown();
  }

  // Replaces existing credentials with insecure credentials.
  void UseInsecureCredentials() {
    server_creds_ = InsecureServerCredentials();
    channel_creds_ = InsecureChannelCredentials();
  }

  // Creates server with gRPC authorization enabled when provider is not null.
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
    // Override target name for host name check
    args.SetSslTargetNameOverride("foo.test.google.fr");
    return grpc::CreateCustomChannel(server_address_, channel_creds_, args);
  }

  grpc::Status SendRpc(const std::shared_ptr<Channel>& channel,
                       ClientContext* context,
                       grpc::testing::EchoResponse* response = nullptr) {
    auto stub = grpc::testing::EchoTestService::NewStub(channel);
    grpc::testing::EchoRequest request;
    request.set_message(kMessage);
    return stub->Echo(context, request, response);
  }

  void CompareAuditLogEntry(absl::string_view output,
                            absl::string_view expected) {
    auto json_or = grpc_core::JsonParse(output);
    ASSERT_TRUE(json_or.ok());
    ASSERT_NE(*json_or->object().find("grpc_audit_log"),
              *json_or->object().end());
    auto inner_json = *json_or->object().find("grpc_audit_log");
    ASSERT_EQ(inner_json.second.type(), grpc_core::Json::Type::kObject);
    auto object = inner_json.second.object();
    object.erase("timestamp");
    auto got = grpc_core::JsonDump(grpc_core::Json::FromObject(object));
    EXPECT_EQ(got, expected);
  }

  std::string server_address_;
  TestServiceImpl service_;
  std::unique_ptr<Server> server_;
  std::shared_ptr<ServerCredentials> server_creds_;
  std::shared_ptr<ChannelCredentials> channel_creds_;
  int log_count_ = 0;
};

TEST_F(GrpcAuthzEnd2EndTest,
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

TEST_F(GrpcAuthzEnd2EndTest, StaticInitDeniesRpcRequestNoMatchInAllowAndDeny) {
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

TEST_F(GrpcAuthzEnd2EndTest,
       StaticInitDeniesRpcRequestMatchInDenyMatchInAllow) {
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

TEST_F(GrpcAuthzEnd2EndTest,
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

TEST_F(GrpcAuthzEnd2EndTest, StaticInitAllowsRpcRequestEmptyDenyMatchInAllow) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_echo\","
      "      \"request\": {"
      "        \"paths\": ["
      "          \"*\""
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

TEST_F(GrpcAuthzEnd2EndTest,
       StaticInitDeniesRpcRequestEmptyDenyNoMatchInAllow) {
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
    GrpcAuthzEnd2EndTest,
    StaticInitDeniesRpcRequestWithPrincipalsFieldOnUnauthenticatedConnection) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_mtls\","
      "      \"source\": {"
      "        \"principals\": [\"*\"]"
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

TEST_F(GrpcAuthzEnd2EndTest,
       StaticInitAllowsRpcRequestWithPrincipalsFieldOnAuthenticatedConnection) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_mtls\","
      "      \"source\": {"
      "        \"principals\": [\"*\"]"
      "      }"
      "    }"
      "  ]"
      "}";
  InitServer(CreateStaticAuthzPolicyProvider(policy));
  auto channel = BuildChannel();
  ClientContext context;
  grpc::testing::EchoResponse resp;
  grpc::Status status = SendRpc(channel, &context, &resp);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp.message(), kMessage);
}

TEST_F(GrpcAuthzEnd2EndTest, StaticInitWithAuditLoggingOnDeny) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_foo\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"key-foo\","
      "            \"values\": [\"foo\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_bar\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"key-bar\","
      "            \"values\": [\"bar\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_condition\": \"ON_DENY\","
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": \"test_logger\""
      "      },"
      "      {"
      "        \"name\": \"stdout_logger\""
      "      }"
      "    ]"
      "  }"
      "}";
  InitServer(CreateStaticAuthzPolicyProvider(policy));
  auto channel = BuildChannel();
  grpc::testing::EchoResponse resp;
  grpc::Status status;

  ClientContext context1;
  // Matches the allow rule.
  context1.AddMetadata("key-foo", "foo");
  status = SendRpc(channel, &context1, &resp);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(log_count_, 0);

  ClientContext context2;
  // Does not match the allow rule or deny rule.
  context2.AddMetadata("key-foo", "bar");
  ::testing::internal::CaptureStdout();
  status = SendRpc(channel, &context2, &resp);
  CompareAuditLogEntry(
      ::testing::internal::GetCapturedStdout(),
      "{\"authorized\":false,\"matched_rule\":\"\",\"policy_name\":\"authz\","
      "\"principal\":\"spiffe://foo.com/bar/baz\",\"rpc_method\":\"/"
      "grpc.testing.EchoTestService/Echo\"}");
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_EQ(log_count_, 1);

  ClientContext context3;
  // Matches the deny rule.
  context3.AddMetadata("key-bar", "bar");
  ::testing::internal::CaptureStdout();
  status = SendRpc(channel, &context3, &resp);
  CompareAuditLogEntry(
      ::testing::internal::GetCapturedStdout(),
      "{\"authorized\":false,\"matched_rule\":\"deny_bar\",\"policy_name\":"
      "\"authz\","
      "\"principal\":\"spiffe://foo.com/bar/baz\",\"rpc_method\":\"/"
      "grpc.testing.EchoTestService/Echo\"}");
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_EQ(log_count_, 2);
}

TEST_F(GrpcAuthzEnd2EndTest, StaticInitWithAuditLoggingOnAllow) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_foo\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"key-foo\","
      "            \"values\": [\"foo\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_bar\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"key-bar\","
      "            \"values\": [\"bar\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_condition\": \"ON_ALLOW\","
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": \"test_logger\""
      "      },"
      "      {"
      "        \"name\": \"stdout_logger\""
      "      }"
      "    ]"
      "  }"
      "}";
  InitServer(CreateStaticAuthzPolicyProvider(policy));
  auto channel = BuildChannel();
  grpc::testing::EchoResponse resp;
  grpc::Status status;

  ClientContext context1;
  context1.AddMetadata("key-foo", "foo");
  ::testing::internal::CaptureStdout();
  status = SendRpc(channel, &context1, &resp);
  CompareAuditLogEntry(
      ::testing::internal::GetCapturedStdout(),
      "{\"authorized\":true,\"matched_rule\":\"allow_foo\",\"policy_name\":"
      "\"authz\","
      "\"principal\":\"spiffe://foo.com/bar/baz\",\"rpc_method\":\"/"
      "grpc.testing.EchoTestService/Echo\"}");
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(log_count_, 1);

  ClientContext context2;
  context2.AddMetadata("key-foo", "bar");
  status = SendRpc(channel, &context2, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_EQ(log_count_, 1);

  ClientContext context3;
  context3.AddMetadata("key-foo", "foo");
  context3.AddMetadata("key-bar", "bar");
  status = SendRpc(channel, &context3, &resp);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_EQ(log_count_, 1);
}

TEST_F(GrpcAuthzEnd2EndTest, StaticInitWithAuditLoggingOnDenyAndAllow) {
  std::string policy =
      "{"
      "  \"name\": \"authz\","
      "  \"allow_rules\": ["
      "    {"
      "      \"name\": \"allow_foo\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"key-foo\","
      "            \"values\": [\"foo\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"deny_rules\": ["
      "    {"
      "      \"name\": \"deny_bar\","
      "      \"request\": {"
      "        \"headers\": ["
      "          {"
      "            \"key\": \"key-bar\","
      "            \"values\": [\"bar\"]"
      "          }"
      "        ]"
      "      }"
      "    }"
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_condition\": \"ON_DENY_AND_ALLOW\","
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": \"test_logger\""
      "      },"
      "      {"
      "        \"name\": \"stdout_logger\""
      "      }"
      "    ]"
      "  }"
      "}";
  InitServer(CreateStaticAuthzPolicyProvider(policy));
  auto channel = BuildChannel();
  grpc::testing::EchoResponse resp;
  grpc::Status status;

  ClientContext context1;
  context1.AddMetadata("key-foo", "foo");
  ::testing::internal::CaptureStdout();
  status = SendRpc(channel, &context1, &resp);
  CompareAuditLogEntry(
      ::testing::internal::GetCapturedStdout(),
      "{\"authorized\":true,\"matched_rule\":\"allow_foo\",\"policy_name\":"
      "\"authz\","
      "\"principal\":\"spiffe://foo.com/bar/baz\",\"rpc_method\":\"/"
      "grpc.testing.EchoTestService/Echo\"}");
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(log_count_, 1);

  ClientContext context2;
  context2.AddMetadata("key-foo", "bar");
  ::testing::internal::CaptureStdout();
  status = SendRpc(channel, &context2, &resp);
  CompareAuditLogEntry(
      ::testing::internal::GetCapturedStdout(),
      "{\"authorized\":false,\"matched_rule\":\"\",\"policy_name\":\"authz\","
      "\"principal\":\"spiffe://foo.com/bar/baz\",\"rpc_method\":\"/"
      "grpc.testing.EchoTestService/Echo\"}");
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_EQ(log_count_, 2);

  ClientContext context3;
  context3.AddMetadata("key-foo", "foo");
  context3.AddMetadata("key-bar", "bar");
  ::testing::internal::CaptureStdout();
  status = SendRpc(channel, &context3, &resp);
  CompareAuditLogEntry(
      ::testing::internal::GetCapturedStdout(),
      "{\"authorized\":false,\"matched_rule\":\"deny_bar\",\"policy_name\":"
      "\"authz\","
      "\"principal\":\"spiffe://foo.com/bar/baz\",\"rpc_method\":\"/"
      "grpc.testing.EchoTestService/Echo\"}");
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_EQ(log_count_, 3);
}

TEST_F(GrpcAuthzEnd2EndTest,
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

TEST_F(GrpcAuthzEnd2EndTest,
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

TEST_F(GrpcAuthzEnd2EndTest,
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

TEST_F(GrpcAuthzEnd2EndTest,
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

TEST_F(GrpcAuthzEnd2EndTest,
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

TEST_F(GrpcAuthzEnd2EndTest,
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

TEST_F(GrpcAuthzEnd2EndTest, FileWatcherValidPolicyRefresh) {
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
  auto provider = CreateFileWatcherAuthzPolicyProvider(tmp_policy.name(), 1);
  InitServer(provider);
  auto channel = BuildChannel();
  ClientContext context1;
  grpc::testing::EchoResponse resp1;
  grpc::Status status = SendRpc(channel, &context1, &resp1);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp1.message(), kMessage);
  gpr_event on_reload_done;
  gpr_event_init(&on_reload_done);
  std::function<void(bool contents_changed, absl::Status status)> callback =
      [&on_reload_done](bool contents_changed, absl::Status status) {
        if (contents_changed) {
          EXPECT_TRUE(status.ok());
          gpr_event_set(&on_reload_done, reinterpret_cast<void*>(1));
        }
      };
  dynamic_cast<grpc_core::FileWatcherAuthorizationPolicyProvider*>(
      provider->c_provider())
      ->SetCallbackForTesting(std::move(callback));
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
  // Wait for the provider's refresh thread to read the updated files.
  ASSERT_EQ(
      gpr_event_wait(&on_reload_done, gpr_inf_future(GPR_CLOCK_MONOTONIC)),
      reinterpret_cast<void*>(1));
  ClientContext context2;
  grpc::testing::EchoResponse resp2;
  status = SendRpc(channel, &context2, &resp2);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp2.message().empty());
  dynamic_cast<grpc_core::FileWatcherAuthorizationPolicyProvider*>(
      provider->c_provider())
      ->SetCallbackForTesting(nullptr);
}

TEST_F(GrpcAuthzEnd2EndTest, FileWatcherInvalidPolicyRefreshSkipsReload) {
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
  auto provider = CreateFileWatcherAuthzPolicyProvider(tmp_policy.name(), 1);
  InitServer(provider);
  auto channel = BuildChannel();
  ClientContext context1;
  grpc::testing::EchoResponse resp1;
  grpc::Status status = SendRpc(channel, &context1, &resp1);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp1.message(), kMessage);
  gpr_event on_reload_done;
  gpr_event_init(&on_reload_done);
  std::function<void(bool contents_changed, absl::Status status)> callback =
      [&on_reload_done](bool contents_changed, absl::Status status) {
        if (contents_changed) {
          EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
          EXPECT_EQ(status.message(), "\"name\" field is not present.");
          gpr_event_set(&on_reload_done, reinterpret_cast<void*>(1));
        }
      };
  dynamic_cast<grpc_core::FileWatcherAuthorizationPolicyProvider*>(
      provider->c_provider())
      ->SetCallbackForTesting(std::move(callback));
  // Replaces existing policy with an invalid authorization policy.
  policy = "{}";
  tmp_policy.RewriteFile(policy);
  // Wait for the provider's refresh thread to read the updated files.
  ASSERT_EQ(
      gpr_event_wait(&on_reload_done, gpr_inf_future(GPR_CLOCK_MONOTONIC)),
      reinterpret_cast<void*>(1));
  ClientContext context2;
  grpc::testing::EchoResponse resp2;
  status = SendRpc(channel, &context2, &resp2);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp2.message(), kMessage);
  dynamic_cast<grpc_core::FileWatcherAuthorizationPolicyProvider*>(
      provider->c_provider())
      ->SetCallbackForTesting(nullptr);
}

TEST_F(GrpcAuthzEnd2EndTest, FileWatcherWithAuditLoggingRecoversFromFailure) {
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
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_condition\": \"ON_ALLOW\","
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": \"test_logger\""
      "      },"
      "      {"
      "        \"name\": \"stdout_logger\""
      "      }"
      "    ]"
      "  }"
      "}";
  grpc_core::testing::TmpFile tmp_policy(policy);
  auto provider = CreateFileWatcherAuthzPolicyProvider(tmp_policy.name(), 1);
  InitServer(provider);
  auto channel = BuildChannel();
  ClientContext context1;
  grpc::testing::EchoResponse resp1;
  ::testing::internal::CaptureStdout();
  grpc::Status status = SendRpc(channel, &context1, &resp1);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp1.message(), kMessage);
  EXPECT_EQ(log_count_, 1);
  CompareAuditLogEntry(
      ::testing::internal::GetCapturedStdout(),
      "{\"authorized\":true,\"matched_rule\":\"allow_echo\",\"policy_name\":"
      "\"authz\",\"principal\":\"spiffe://foo.com/bar/baz\",\"rpc_method\":\"/"
      "grpc.testing.EchoTestService/Echo\"}");
  gpr_event on_first_reload_done;
  gpr_event_init(&on_first_reload_done);
  std::function<void(bool contents_changed, absl::Status status)> callback1 =
      [&on_first_reload_done](bool contents_changed, absl::Status status) {
        if (contents_changed) {
          EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
          EXPECT_EQ(status.message(), "\"name\" field is not present.");
          gpr_event_set(&on_first_reload_done, reinterpret_cast<void*>(1));
        }
      };
  dynamic_cast<grpc_core::FileWatcherAuthorizationPolicyProvider*>(
      provider->c_provider())
      ->SetCallbackForTesting(std::move(callback1));
  // Replaces existing policy with an invalid authorization policy.
  policy = "{}";
  tmp_policy.RewriteFile(policy);
  // Wait for the provider's refresh thread to read the updated files.
  ASSERT_EQ(gpr_event_wait(&on_first_reload_done,
                           gpr_inf_future(GPR_CLOCK_MONOTONIC)),
            reinterpret_cast<void*>(1));
  ClientContext context2;
  grpc::testing::EchoResponse resp2;
  ::testing::internal::CaptureStdout();
  status = SendRpc(channel, &context2, &resp2);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(resp2.message(), kMessage);
  EXPECT_EQ(log_count_, 2);
  CompareAuditLogEntry(
      ::testing::internal::GetCapturedStdout(),
      "{\"authorized\":true,\"matched_rule\":\"allow_echo\",\"policy_name\":"
      "\"authz\",\"principal\":\"spiffe://foo.com/bar/baz\",\"rpc_method\":\"/"
      "grpc.testing.EchoTestService/Echo\"}");
  gpr_event on_second_reload_done;
  gpr_event_init(&on_second_reload_done);
  std::function<void(bool contents_changed, absl::Status status)> callback2 =
      [&on_second_reload_done](bool contents_changed, absl::Status status) {
        if (contents_changed) {
          EXPECT_TRUE(status.ok());
          gpr_event_set(&on_second_reload_done, reinterpret_cast<void*>(1));
        }
      };
  dynamic_cast<grpc_core::FileWatcherAuthorizationPolicyProvider*>(
      provider->c_provider())
      ->SetCallbackForTesting(std::move(callback2));
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
      "  ],"
      "  \"audit_logging_options\": {"
      "    \"audit_condition\": \"ON_DENY\","
      "    \"audit_loggers\": ["
      "      {"
      "        \"name\": \"test_logger\""
      "      },"
      "      {"
      "        \"name\": \"stdout_logger\""
      "      }"
      "    ]"
      "  }"
      "}";
  tmp_policy.RewriteFile(policy);
  // Wait for the provider's refresh thread to read the updated files.
  ASSERT_EQ(gpr_event_wait(&on_second_reload_done,
                           gpr_inf_future(GPR_CLOCK_MONOTONIC)),
            reinterpret_cast<void*>(1));
  ClientContext context3;
  grpc::testing::EchoResponse resp3;
  ::testing::internal::CaptureStdout();
  status = SendRpc(channel, &context3, &resp3);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Unauthorized RPC request rejected.");
  EXPECT_TRUE(resp3.message().empty());
  EXPECT_EQ(log_count_, 3);
  CompareAuditLogEntry(
      ::testing::internal::GetCapturedStdout(),
      "{\"authorized\":false,\"matched_rule\":\"deny_echo\",\"policy_name\":"
      "\"authz\",\"principal\":\"spiffe://foo.com/bar/baz\",\"rpc_method\":\"/"
      "grpc.testing.EchoTestService/Echo\"}");
  dynamic_cast<grpc_core::FileWatcherAuthorizationPolicyProvider*>(
      provider->c_provider())
      ->SetCallbackForTesting(nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  const auto result = RUN_ALL_TESTS();
  return result;
}
