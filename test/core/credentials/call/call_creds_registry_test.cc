//
// Copyright 2025 gRPC authors.
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

#include "src/core/credentials/call/call_creds_registry.h"

#include "envoy/extensions/grpc_service/call_credentials/access_token/v3/access_token_credentials.pb.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/call/jwt_token_file/jwt_token_file_call_credentials.h"
#include "src/core/credentials/call/oauth2/oauth2_credentials.h"
#include "test/core/test_util/test_call_creds.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

class TestCallCredsFactory : public CallCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }
  RefCountedPtr<const CallCredsConfig> ParseConfig(
      const Json& /*config*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  absl::string_view proto_type() const override { return ProtoType(); }
  RefCountedPtr<const CallCredsConfig> ParseProto(
      absl::string_view /*serialized_proto*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  RefCountedPtr<grpc_call_credentials> CreateCallCreds(
      RefCountedPtr<const CallCredsConfig> /*config*/) const override {
    return MakeRefCounted<grpc_md_only_test_credentials>("key", "value");
  }

 private:
  class Config : public CallCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }
    absl::string_view proto_type() const override { return ProtoType(); }
    bool Equals(const CallCredsConfig&) const override { return true; }
    std::string ToString() const override { return "{}"; }
  };

  static absl::string_view Type() { return "test"; }
  static absl::string_view ProtoType() { return "io.grpc.TestCreds"; }
};

class CallCredsRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override { CoreConfiguration::Reset(); }

  // Run a basic test for a given credential type.
  // type is the string identifying the type in the registry.
  // credential_type is the resulting type of the actual channel creds object.
  void TestConfig(absl::string_view type, Json json,
                  absl::string_view expected_config,
                  UniqueTypeName expected_credential_type) {
    auto& registry = CoreConfiguration::Get().call_creds_registry();
    EXPECT_TRUE(registry.IsSupported(type));
    ValidationErrors errors;
    auto config = registry.ParseConfig(type, json, JsonArgs(), &errors);
    ASSERT_TRUE(errors.ok()) << errors.message("unexpected errors");
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->type(), type);
    EXPECT_EQ(config->ToString(), expected_config);
    auto creds = registry.CreateCallCreds(std::move(config));
    ASSERT_NE(creds, nullptr);
    UniqueTypeName actual_type = creds->type();
    EXPECT_EQ(actual_type, expected_credential_type)
        << "Actual: " << actual_type.name()
        << "\nExpected: " << expected_credential_type.name();
  }

  // Run a basic test for a given credential type from proto config.
  // type is the string identifying the type in the registry.
  // credential_type is the resulting type of the actual channel creds object.
  void TestProto(absl::string_view proto_type,
                 absl::string_view serialized_proto,
                 absl::string_view expected_config,
                 UniqueTypeName expected_credential_type) {
    auto& registry = CoreConfiguration::Get().call_creds_registry();
    EXPECT_TRUE(registry.IsProtoSupported(proto_type));
    ValidationErrors errors;
    auto config = registry.ParseProto(proto_type, serialized_proto, &errors);
    ASSERT_TRUE(errors.ok()) << errors.message("unexpected errors");
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->proto_type(), proto_type);
    EXPECT_EQ(config->ToString(), expected_config);
    auto creds = registry.CreateCallCreds(std::move(config));
    ASSERT_NE(creds, nullptr);
    UniqueTypeName actual_type = creds->type();
    EXPECT_EQ(actual_type, expected_credential_type)
        << "Actual: " << actual_type.name()
        << "\nExpected: " << expected_credential_type.name();
  }
};

TEST_F(CallCredsRegistryTest, JwtTokenFileCreds) {
  Json json = Json::FromObject({
      {"jwt_token_file", Json::FromString("/path/to/cert_file")},
  });
  TestConfig("jwt_token_file", json, "{path=\"/path/to/cert_file\"}",
             JwtTokenFileCallCredentials::Type());
}

TEST_F(CallCredsRegistryTest, JwtTokenFileCredsMissingRequiredField) {
  Json json = Json::FromObject({});
  ValidationErrors errors;
  auto config = CoreConfiguration::Get().call_creds_registry().ParseConfig(
      "jwt_token_file", json, JsonArgs(), &errors);
  EXPECT_EQ(errors.message("errors"),
            "errors: [field:jwt_token_file error:field not present]");
}

TEST_F(CallCredsRegistryTest, AccessTokenCreds) {
  envoy::extensions::grpc_service::call_credentials::access_token::v3::
      AccessTokenCredentials proto;
  proto.set_token("foo");
  TestProto(
      "envoy.extensions.grpc_service.call_credentials.access_token.v3"
      ".AccessTokenCredentials",
      proto.SerializeAsString(), "{token=\"foo\"}",
      grpc_access_token_credentials::Type());
}

TEST_F(CallCredsRegistryTest, AccessTokenCredsTokenNotSet) {
  envoy::extensions::grpc_service::call_credentials::access_token::v3::
      AccessTokenCredentials proto;
  ValidationErrors errors;
  auto config = CoreConfiguration::Get().call_creds_registry().ParseProto(
      "envoy.extensions.grpc_service.call_credentials.access_token.v3"
      ".AccessTokenCredentials",
      proto.SerializeAsString(), &errors);
  EXPECT_EQ(errors.message("errors"),
            "errors: [field:token error:field not present]");
}

TEST_F(CallCredsRegistryTest, Register) {
  // Before registration.
  EXPECT_FALSE(
      CoreConfiguration::Get().call_creds_registry().IsSupported("test"));
  ValidationErrors errors;
  auto config = CoreConfiguration::Get().call_creds_registry().ParseConfig(
      "test", Json::FromObject({}), JsonArgs(), &errors);
  EXPECT_TRUE(errors.ok()) << errors.message("unexpected errors");
  EXPECT_EQ(config, nullptr);
  auto creds = CoreConfiguration::Get().call_creds_registry().CreateCallCreds(
      std::move(config));
  EXPECT_EQ(creds, nullptr);
  // Registration.
  CoreConfiguration::WithSubstituteBuilder builder(
      [](CoreConfiguration::Builder* builder) {
        BuildCoreConfiguration(builder);
        builder->call_creds_registry()->RegisterCallCredsFactory(
            std::make_unique<TestCallCredsFactory>());
      });
  // After registration.
  // Parse config from JSON.
  EXPECT_TRUE(
      CoreConfiguration::Get().call_creds_registry().IsSupported("test"));
  config = CoreConfiguration::Get().call_creds_registry().ParseConfig(
      "test", Json::FromObject({}), JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.message("unexpected errors");
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->type(), "test");
  creds = CoreConfiguration::Get().call_creds_registry().CreateCallCreds(
      std::move(config));
  ASSERT_NE(creds, nullptr);
  EXPECT_EQ(creds->type(), grpc_md_only_test_credentials::Type());
  // Parse config from proto.
  EXPECT_TRUE(CoreConfiguration::Get().call_creds_registry().IsProtoSupported(
      "io.grpc.TestCreds"));
  config = CoreConfiguration::Get().call_creds_registry().ParseProto(
      "io.grpc.TestCreds", "", &errors);
  ASSERT_TRUE(errors.ok()) << errors.message("unexpected errors");
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->proto_type(), "io.grpc.TestCreds");
  creds = CoreConfiguration::Get().call_creds_registry().CreateCallCreds(
      std::move(config));
  ASSERT_NE(creds, nullptr);
  EXPECT_EQ(creds->type(), grpc_md_only_test_credentials::Type());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
