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

#include "src/core/credentials/transport/channel_creds_registry.h"

#include <grpc/grpc.h>

#include <optional>

#include "envoy/extensions/grpc_service/channel_credentials/google_default/v3/google_default_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/tls/v3/tls_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/xds/v3/xds_credentials.pb.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/composite/composite_channel_credentials.h"
#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/credentials/transport/insecure/insecure_credentials.h"
#include "src/core/credentials/transport/tls/tls_credentials.h"
#include "src/core/credentials/transport/xds/xds_credentials.h"
#include "src/core/xds/grpc/certificate_provider_store.h"
#include "src/core/xds/grpc/file_watcher_certificate_provider_factory.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

class TestChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }
  RefCountedPtr<const ChannelCredsConfig> ParseConfig(
      const Json& /*config*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  absl::string_view proto_type() const override { return ProtoType(); }
  RefCountedPtr<const ChannelCredsConfig> ParseProto(
      absl::string_view /*serialized_proto*/,
      const CertificateProviderStoreInterface::PluginDefinitionMap&
          /*certificate_provider_definitions*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<const ChannelCredsConfig> /*config*/,
      const CertificateProviderStoreInterface&
          /*certificate_provider_store*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_fake_transport_security_credentials_create());
  }

 private:
  class Config : public ChannelCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }
    absl::string_view proto_type() const override { return ProtoType(); }
    bool Equals(const ChannelCredsConfig&) const override { return true; }
    std::string ToString() const override { return "{}"; }
  };

  static absl::string_view Type() { return "test"; }
  static absl::string_view ProtoType() { return "io.grpc.TestCreds"; }
};

class ChannelCredsRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    cert_provider_store_ = MakeRefCounted<CertificateProviderStore>(
        CertificateProviderStoreInterface::PluginDefinitionMap{});
    CoreConfiguration::Reset();
  }

  // Run a basic test for a given credential type from a JSON config.
  // type is the string identifying the type in the registry.
  // credential_type is the resulting type of the actual channel creds object;
  // if nullopt, does not attempt to instantiate the credentials.
  void TestCredsConfig(absl::string_view type,
                       absl::string_view expected_config,
                       std::optional<UniqueTypeName> credential_type,
                       Json json = Json::FromObject({})) {
    EXPECT_TRUE(
        CoreConfiguration::Get().channel_creds_registry().IsSupported(type));
    ValidationErrors errors;
    auto config = CoreConfiguration::Get().channel_creds_registry().ParseConfig(
        type, json, JsonArgs(), &errors);
    ASSERT_TRUE(errors.ok()) << errors.message("unexpected errors");
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->type(), type);
    EXPECT_EQ(config->ToString(), expected_config);
    if (credential_type.has_value()) {
      auto creds =
          CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
              std::move(config), *cert_provider_store_);
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

  // Run a basic test for a given credential type from a protobuf.
  // type is the string identifying the type in the registry.
  // credential_type is the resulting type of the actual channel creds object;
  // if nullopt, does not attempt to instantiate the credentials.
  void TestCredsProto(absl::string_view proto_type,
                      absl::string_view serialized_proto,
                      absl::string_view expected_config,
                      std::optional<UniqueTypeName> credential_type) {
    EXPECT_TRUE(
        CoreConfiguration::Get().channel_creds_registry().IsProtoSupported(
            proto_type));
    ValidationErrors errors;
    auto config = CoreConfiguration::Get().channel_creds_registry().ParseProto(
        proto_type, serialized_proto, cert_provider_map_, &errors);
    ASSERT_TRUE(errors.ok()) << errors.message("unexpected errors");
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->proto_type(), proto_type);
    EXPECT_EQ(config->ToString(), expected_config);
    if (credential_type.has_value()) {
      auto creds =
          CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
              std::move(config), *cert_provider_store_);
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

  void AddFileWatcherProvider(
      const std::string& instance_name, const std::string& certificate_file,
      const std::string& private_key_file,
      const std::string& ca_certificate_file, Duration refresh_interval) {
    Json::Object json_obj;
    if (!certificate_file.empty()) {
      json_obj["certificate_file"] = Json::FromString(certificate_file);
    }
    if (!private_key_file.empty()) {
      json_obj["private_key_file"] = Json::FromString(private_key_file);
    }
    if (!ca_certificate_file.empty()) {
      json_obj["ca_certificate_file"] = Json::FromString(ca_certificate_file);
    }
    if (refresh_interval != Duration::Zero()) {
      json_obj["refresh_interval"] =
          Json::FromString(refresh_interval.ToJsonString());
    }
    FileWatcherCertificateProviderFactory cert_provider_factory;
    ValidationErrors errors;
    auto config = cert_provider_factory.CreateCertificateProviderConfig(
        Json::FromObject(std::move(json_obj)), JsonArgs(), &errors);
    ASSERT_TRUE(errors.ok()) << errors.message("unexpected errors");
    cert_provider_map_[instance_name] = {instance_name, std::move(config)};
  }

  RefCountedPtr<CertificateProviderStore> cert_provider_store_;
  CertificateProviderStoreInterface::PluginDefinitionMap cert_provider_map_;
};

TEST_F(ChannelCredsRegistryTest, GoogleDefaultCreds) {
  // Don't actually instantiate the credentials, since that fails in
  // some environments.
  TestCredsConfig("google_default", "{}", std::nullopt);
}

TEST_F(ChannelCredsRegistryTest, GoogleDefaultCredsProto) {
  // Don't actually instantiate the credentials, since that fails in
  // some environments.
  TestCredsProto(
      "envoy.extensions.grpc_service.channel_credentials.google_default"
      ".v3.GoogleDefaultCredentials",
      envoy::extensions::grpc_service::channel_credentials::google_default::v3::GoogleDefaultCredentials().SerializeAsString(),
      "{}", std::nullopt);
}

TEST_F(ChannelCredsRegistryTest, InsecureCreds) {
  TestCredsConfig("insecure", "{}", InsecureCredentials::Type());
}

TEST_F(ChannelCredsRegistryTest, InsecureCredsProto) {
  TestCredsProto(
      "envoy.extensions.grpc_service.channel_credentials.insecure"
      ".v3.InsecureCredentials",
      envoy::extensions::grpc_service::channel_credentials::insecure::v3::InsecureCredentials().SerializeAsString(),
      "{}", InsecureCredentials::Type());
}

TEST_F(ChannelCredsRegistryTest, FakeCreds) {
  TestCredsConfig("fake", "{}", grpc_fake_channel_credentials::Type());
}

TEST_F(ChannelCredsRegistryTest, TlsCredsNoConfig) {
  TestCredsConfig("tls", "{}", TlsCredentials::Type());
}

TEST_F(ChannelCredsRegistryTest, TlsCredsFullConfig) {
  Json json = Json::FromObject({
      {"certificate_file", Json::FromString("/path/to/cert_file")},
      {"private_key_file", Json::FromString("/path/to/private_key_file")},
      {"ca_certificate_file", Json::FromString("/path/to/ca_cert_file")},
      {"refresh_interval", Json::FromString("1s")},
  });
  TestCredsConfig(
      "tls",
      "{certificate_file=/path/to/cert_file,"
      "private_key_file=/path/to/private_key_file,"
      "ca_certificate_file=/path/to/ca_cert_file,"
      "refresh_interval=1000ms}",
      TlsCredentials::Type(), json);
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

TEST_F(ChannelCredsRegistryTest, TlsCredsConfigCertFileWithoutPrivateKeyFile) {
  Json json = Json::FromObject({
      {"certificate_file", Json::FromString("/path/to/cert_file")},
  });
  ValidationErrors errors;
  auto config = CoreConfiguration::Get().channel_creds_registry().ParseConfig(
      "tls", json, JsonArgs(), &errors);
  EXPECT_EQ(errors.message("errors"),
            "errors: ["
            "field: error:fields \"certificate_file\" and "
            "\"private_key_file\" must be both set or both unset]");
}

TEST_F(ChannelCredsRegistryTest, TlsCredsConfigPrivateKeyFileWithoutCertFile) {
  Json json = Json::FromObject({
      {"private_key_file", Json::FromString("/path/to/private_key_file")},
  });
  ValidationErrors errors;
  auto config = CoreConfiguration::Get().channel_creds_registry().ParseConfig(
      "tls", json, JsonArgs(), &errors);
  EXPECT_EQ(errors.message("errors"),
            "errors: ["
            "field: error:fields \"certificate_file\" and "
            "\"private_key_file\" must be both set or both unset]");
}

TEST_F(ChannelCredsRegistryTest, TlsCredsProto) {
  // Add config for "foo" cert provider.
  AddFileWatcherProvider(
      /*instance_name=*/"foo", /*certificate_file=*/"/path/to/cert/file",
      /*private_key_file=*/"/path/to/private_key",
      /*ca_certificate_file=*/"/path/to/ca/cert",
      /*refresh_interval=*/Duration::Seconds(1));
  // Now construct TlsCredentials extension proto.
  envoy::extensions::grpc_service::channel_credentials::tls::v3::TlsCredentials
      tls_creds_proto;
  auto* cert_provider = tls_creds_proto.mutable_root_certificate_provider();
  cert_provider->set_instance_name("foo");
  // Test parsing.
  TestCredsProto(
      "envoy.extensions.grpc_service.channel_credentials.tls.v3.TlsCredentials",
      tls_creds_proto.SerializeAsString(),
      "{root_cert_provider={instance_name=\"foo\"}}", TlsCredentials::Type());
}

TEST_F(ChannelCredsRegistryTest, TlsCredsProtoAllFields) {
  // Add config for "foo" and "bar" cert providers.
  AddFileWatcherProvider(
      /*instance_name=*/"foo", /*certificate_file=*/"/path/to/cert/file",
      /*private_key_file=*/"/path/to/private_key",
      /*ca_certificate_file=*/"/path/to/ca/cert",
      /*refresh_interval=*/Duration::Seconds(1));
  AddFileWatcherProvider(
      /*instance_name=*/"bar", /*certificate_file=*/"/path/to/cert/file",
      /*private_key_file=*/"/path/to/private_key",
      /*ca_certificate_file=*/"/path/to/ca/cert",
      /*refresh_interval=*/Duration::Seconds(1));
  // Now construct TlsCredentials extension proto.
  envoy::extensions::grpc_service::channel_credentials::tls::v3::TlsCredentials
      tls_creds_proto;
  auto* cert_provider = tls_creds_proto.mutable_root_certificate_provider();
  cert_provider->set_instance_name("foo");
  cert_provider->set_certificate_name("baz");
  cert_provider = tls_creds_proto.mutable_identity_certificate_provider();
  cert_provider->set_instance_name("bar");
  cert_provider->set_certificate_name("quux");
  *tls_creds_proto.mutable_identity_certificate_provider() = *cert_provider;
  // Test parsing.
  TestCredsProto(
      "envoy.extensions.grpc_service.channel_credentials.tls.v3.TlsCredentials",
      tls_creds_proto.SerializeAsString(),
      "{root_cert_provider={instance_name=\"foo\","
      "certificate_name=\"baz\"},"
      "identity_cert_provider={instance_name=\"bar\","
      "certificate_name=\"quux\"}}",
      TlsCredentials::Type());
}

TEST_F(ChannelCredsRegistryTest, TlsCredsProtoErrors) {
  // Construct TlsCredentials extension proto with errors.
  envoy::extensions::grpc_service::channel_credentials::tls::v3::TlsCredentials
      tls_creds_proto;
  auto* cert_provider = tls_creds_proto.mutable_identity_certificate_provider();
  cert_provider->set_instance_name("foo");  // Does not exist.
  // Test parsing.
  ValidationErrors errors;
  auto config = CoreConfiguration::Get().channel_creds_registry().ParseProto(
      "envoy.extensions.grpc_service.channel_credentials.tls.v3.TlsCredentials",
      tls_creds_proto.SerializeAsString(), cert_provider_map_, &errors);
  EXPECT_EQ(errors.message("errors"),
            "errors: ["
            "field:identity_certificate_provider.instance_name "
            "error:unrecognized certificate provider instance name: foo; "
            "field:root_certificate_provider error:field not set]");
}

TEST_F(ChannelCredsRegistryTest, XdsCredsProto) {
  // Construct XdsCredentials extension proto.
  envoy::extensions::grpc_service::channel_credentials::xds::v3::XdsCredentials
      xds_creds_proto;
  xds_creds_proto.mutable_fallback_credentials()->PackFrom(
      envoy::extensions::grpc_service::channel_credentials::insecure::v3::InsecureCredentials());
  // Test parsing.
  TestCredsProto(
      "envoy.extensions.grpc_service.channel_credentials.xds.v3.XdsCredentials",
      xds_creds_proto.SerializeAsString(),
      "{fallback_creds={type=insecure, config={}}}",
      XdsCredentials::Type());
}

TEST_F(ChannelCredsRegistryTest, XdsCredsProtoMissingField) {
  envoy::extensions::grpc_service::channel_credentials::xds::v3::XdsCredentials
      xds_creds_proto;
  ValidationErrors errors;
  auto config = CoreConfiguration::Get().channel_creds_registry().ParseProto(
      "envoy.extensions.grpc_service.channel_credentials.xds.v3.XdsCredentials",
      xds_creds_proto.SerializeAsString(), cert_provider_map_, &errors);
  EXPECT_EQ(errors.message("errors"),
            "errors: [field:fallback_credentials error:field not set]");
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
          std::move(config), *cert_provider_store_);
  EXPECT_EQ(creds, nullptr);
  // Registration.
  CoreConfiguration::WithSubstituteBuilder builder(
      [](CoreConfiguration::Builder* builder) {
        BuildCoreConfiguration(builder);
        builder->channel_creds_registry()->RegisterChannelCredsFactory(
            std::make_unique<TestChannelCredsFactory>());
      });
  // After registration.
  // Check parsing from JSON.
  EXPECT_TRUE(
      CoreConfiguration::Get().channel_creds_registry().IsSupported("test"));
  config = CoreConfiguration::Get().channel_creds_registry().ParseConfig(
      "test", Json::FromObject({}), JsonArgs(), &errors);
  ASSERT_TRUE(errors.ok()) << errors.message("unexpected errors");
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->type(), "test");
  creds = CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
      std::move(config), *cert_provider_store_);
  ASSERT_NE(creds, nullptr);
  EXPECT_EQ(creds->type(), grpc_fake_channel_credentials::Type());
  // Check parsing from proto.
  EXPECT_TRUE(
      CoreConfiguration::Get().channel_creds_registry().IsProtoSupported("io.grpc.TestCreds"));
  config = CoreConfiguration::Get().channel_creds_registry().ParseProto(
      "io.grpc.TestCreds", "", {}, &errors);
  ASSERT_TRUE(errors.ok()) << errors.message("unexpected errors");
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->type(), "test");
  creds = CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
      std::move(config), *cert_provider_store_);
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
