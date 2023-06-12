//
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
//

#include "src/core/lib/security/credentials/channel_creds_registry.h"

#include "absl/types/optional.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/security/credentials/composite/composite_credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/credentials/insecure/insecure_credentials.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class TestChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }
  RefCountedPtr<ChannelCredsConfig> ParseConfig(
      const Json& /*config*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<ChannelCredsConfig> /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_fake_transport_security_credentials_create());
  }

 private:
  class Config : public ChannelCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }
    bool Equals(const ChannelCredsConfig&) const override { return true; }
    Json ToJson() const override { return Json::FromObject({}); }
  };

  static absl::string_view Type() { return "test"; }
};

class ChannelCredsRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override { CoreConfiguration::Reset(); }

  // Run a basic test for a given credential type.
  // type is the string identifying the type in the registry.
  // credential_type is the resulting type of the actual channel creds object;
  // if nullopt, does not attempt to instantiate the credentials.
  void TestCreds(absl::string_view type,
                 absl::optional<UniqueTypeName> credential_type,
                 Json json = Json::FromObject({})) {
    EXPECT_TRUE(
        CoreConfiguration::Get().channel_creds_registry().IsSupported(type));
    ValidationErrors errors;
    auto config = CoreConfiguration::Get().channel_creds_registry().ParseConfig(
        type, json, JsonArgs(), &errors);
    EXPECT_TRUE(errors.ok()) << errors.message("unexpected errors");
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->type(), type);
    if (credential_type.has_value()) {
      auto creds =
          CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
              std::move(config));
      ASSERT_NE(creds, nullptr);
      UniqueTypeName actual_type = creds->type();
      // If we get composite creds, unwrap them.
      // (This happens for GoogleDefaultCreds.)
      if (creds->type() == grpc_composite_channel_credentials::Type()) {
        actual_type =
            static_cast<grpc_composite_channel_credentials*>(creds.get())
                ->inner_creds()
                ->type();
      }
      EXPECT_EQ(actual_type, *credential_type)
          << "Actual: " << actual_type.name()
          << "\nExpected: " << credential_type->name();
    }
  }
};

TEST_F(ChannelCredsRegistryTest, GoogleDefaultCreds) {
  // Don't actually instantiate the credentials, since that fails in
  // some environments.
  TestCreds("google_default", absl::nullopt);
}

TEST_F(ChannelCredsRegistryTest, InsecureCreds) {
  TestCreds("insecure", InsecureCredentials::Type());
}

TEST_F(ChannelCredsRegistryTest, FakeCreds) {
  TestCreds("fake", grpc_fake_channel_credentials::Type());
}

TEST_F(ChannelCredsRegistryTest, TlsCredsNoConfig) {
  TestCreds("tls", TlsCredentials::Type());
}

TEST_F(ChannelCredsRegistryTest, TlsCredsFullConfig) {
  Json json = Json::FromObject({
      {"certificate_file", Json::FromString("/path/to/cert_file")},
      {"private_key_file", Json::FromString("/path/to/private_key_file")},
      {"ca_certificate_file", Json::FromString("/path/to/ca_cert_file")},
      {"refresh_interval", Json::FromString("1s")},
  });
  TestCreds("tls", TlsCredentials::Type(), json);
}

TEST_F(ChannelCredsRegistryTest, TlsCredsConfigInvalid) {
  Json json = Json::FromObject({
      {"certificate_file", Json::FromObject({})},
      {"private_key_file", Json::FromArray({})},
      {"ca_certificate_file", Json::FromBool(true)},
      {"refresh_interval", Json::FromNumber(1)},
  });
  ValidationErrors errors;
  auto config = CoreConfiguration::Get().channel_creds_registry().ParseConfig(
      "tls", json, JsonArgs(), &errors);
  EXPECT_EQ(errors.message("errors"),
            "errors: ["
            "field:ca_certificate_file error:is not a string; "
            "field:certificate_file error:is not a string; "
            "field:private_key_file error:is not a string; "
            "field:refresh_interval error:is not a string]");
}

TEST_F(ChannelCredsRegistryTest, Register) {
  // Before registration.
  EXPECT_FALSE(
      CoreConfiguration::Get().channel_creds_registry().IsSupported("test"));
  ValidationErrors errors;
  auto config = CoreConfiguration::Get().channel_creds_registry().ParseConfig(
      "test", Json::FromObject({}), JsonArgs(), &errors);
  EXPECT_TRUE(errors.ok()) << errors.message("unexpected errors");
  EXPECT_EQ(config, nullptr);
  auto creds =
      CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
          std::move(config));
  EXPECT_EQ(creds, nullptr);
  // Registration.
  CoreConfiguration::WithSubstituteBuilder builder(
      [](CoreConfiguration::Builder* builder) {
        BuildCoreConfiguration(builder);
        builder->channel_creds_registry()->RegisterChannelCredsFactory(
            std::make_unique<TestChannelCredsFactory>());
      });
  // After registration.
  EXPECT_TRUE(
      CoreConfiguration::Get().channel_creds_registry().IsSupported("test"));
  config = CoreConfiguration::Get().channel_creds_registry().ParseConfig(
      "test", Json::FromObject({}), JsonArgs(), &errors);
  EXPECT_TRUE(errors.ok()) << errors.message("unexpected errors");
  EXPECT_NE(config, nullptr);
  EXPECT_EQ(config->type(), "test");
  creds = CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
      std::move(config));
  ASSERT_NE(creds, nullptr);
  EXPECT_EQ(creds->type(), grpc_fake_channel_credentials::Type());
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
