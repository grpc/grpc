//
// Copyright 2019 gRPC authors.
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

#include <regex>

#include "absl/strings/numbers.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/slice.h>

#include "src/core/ext/xds/certificate_provider_registry.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

class XdsBootstrapTest : public ::testing::Test {
 public:
  XdsBootstrapTest() { grpc_init(); }

  ~XdsBootstrapTest() override { grpc_shutdown_blocking(); }
};

TEST_F(XdsBootstrapTest, Basic) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": ["
      "        {"
      "          \"type\": \"fake\","
      "          \"ignore\": 0"
      "        }"
      "      ],"
      "      \"ignore\": 0"
      "    },"
      "    {"
      "      \"server_uri\": \"ignored\","
      "      \"channel_creds\": ["
      "        {"
      "          \"type\": \"ignored\","
      "          \"ignore\": 0"
      "        }"
      "      ],"
      "      \"ignore\": 0"
      "    }"
      "  ],"
      "  \"node\": {"
      "    \"id\": \"foo\","
      "    \"cluster\": \"bar\","
      "    \"locality\": {"
      "      \"region\": \"milky_way\","
      "      \"zone\": \"sol_system\","
      "      \"subzone\": \"earth\","
      "      \"ignore\": {}"
      "    },"
      "    \"metadata\": {"
      "      \"foo\": 1,"
      "      \"bar\": 2"
      "    },"
      "    \"ignore\": \"whee\""
      "  },"
      "  \"ignore\": {}"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  EXPECT_EQ(bootstrap.server().server_uri, "fake:///lb");
  ASSERT_EQ(bootstrap.server().channel_creds.size(), 1UL);
  EXPECT_EQ(bootstrap.server().channel_creds[0].type, "fake");
  EXPECT_EQ(bootstrap.server().channel_creds[0].config.type(),
            Json::Type::JSON_NULL);
  ASSERT_NE(bootstrap.node(), nullptr);
  EXPECT_EQ(bootstrap.node()->id, "foo");
  EXPECT_EQ(bootstrap.node()->cluster, "bar");
  EXPECT_EQ(bootstrap.node()->locality_region, "milky_way");
  EXPECT_EQ(bootstrap.node()->locality_zone, "sol_system");
  EXPECT_EQ(bootstrap.node()->locality_subzone, "earth");
  ASSERT_EQ(bootstrap.node()->metadata.type(), Json::Type::OBJECT);
  EXPECT_THAT(bootstrap.node()->metadata.object_value(),
              ::testing::ElementsAre(
                  ::testing::Pair(
                      ::testing::Eq("bar"),
                      ::testing::AllOf(
                          ::testing::Property(&Json::type, Json::Type::NUMBER),
                          ::testing::Property(&Json::string_value, "2"))),
                  ::testing::Pair(
                      ::testing::Eq("foo"),
                      ::testing::AllOf(
                          ::testing::Property(&Json::type, Json::Type::NUMBER),
                          ::testing::Property(&Json::string_value, "1")))));
}

TEST_F(XdsBootstrapTest, ValidWithoutChannelCredsAndNode) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\""
      "    }"
      "  ]"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  EXPECT_EQ(bootstrap.server().server_uri, "fake:///lb");
  EXPECT_EQ(bootstrap.server().channel_creds.size(), 0UL);
  EXPECT_EQ(bootstrap.node(), nullptr);
}

TEST_F(XdsBootstrapTest, MissingXdsServers) {
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse("{}", &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_string(error),
              ::testing::ContainsRegex("\"xds_servers\" field not present"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(XdsBootstrapTest, TopFieldsWrongTypes) {
  const char* json_str =
      "{"
      "  \"xds_servers\":1,"
      "  \"node\":1,"
      "  \"certificate_providers\":1"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_string(error),
              ::testing::ContainsRegex(
                  "\"xds_servers\" field is not an array.*"
                  "\"node\" field is not an object.*"
                  "\"certificate_providers\" field is not an object"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(XdsBootstrapTest, XdsServerMissingServerUri) {
  const char* json_str =
      "{"
      "  \"xds_servers\":[{}]"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_string(error),
              ::testing::ContainsRegex("errors parsing \"xds_servers\" array.*"
                                       "errors parsing index 0.*"
                                       "\"server_uri\" field not present"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(XdsBootstrapTest, XdsServerUriAndCredsWrongTypes) {
  const char* json_str =
      "{"
      "  \"xds_servers\":["
      "    {"
      "      \"server_uri\":1,"
      "      \"channel_creds\":1"
      "    }"
      "  ]"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(
      grpc_error_string(error),
      ::testing::ContainsRegex("errors parsing \"xds_servers\" array.*"
                               "errors parsing index 0.*"
                               "\"server_uri\" field is not a string.*"
                               "\"channel_creds\" field is not an array"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(XdsBootstrapTest, ChannelCredsFieldsWrongTypes) {
  const char* json_str =
      "{"
      "  \"xds_servers\":["
      "    {"
      "      \"server_uri\":\"foo\","
      "      \"channel_creds\":["
      "        {"
      "          \"type\":0,"
      "          \"config\":1"
      "        }"
      "      ]"
      "    }"
      "  ]"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(
      grpc_error_string(error),
      ::testing::ContainsRegex("errors parsing \"xds_servers\" array.*"
                               "errors parsing index 0.*"
                               "errors parsing \"channel_creds\" array.*"
                               "errors parsing index 0.*"
                               "\"type\" field is not a string.*"
                               "\"config\" field is not an object"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(XdsBootstrapTest, NodeFieldsWrongTypes) {
  const char* json_str =
      "{"
      "  \"node\":{"
      "    \"id\":0,"
      "    \"cluster\":0,"
      "    \"locality\":0,"
      "    \"metadata\":0"
      "  }"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_string(error),
              ::testing::ContainsRegex("errors parsing \"node\" object.*"
                                       "\"id\" field is not a string.*"
                                       "\"cluster\" field is not a string.*"
                                       "\"locality\" field is not an object.*"
                                       "\"metadata\" field is not an object"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(XdsBootstrapTest, LocalityFieldsWrongType) {
  const char* json_str =
      "{"
      "  \"node\":{"
      "    \"locality\":{"
      "      \"region\":0,"
      "      \"zone\":0,"
      "      \"subzone\":0"
      "    }"
      "  }"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_string(error),
              ::testing::ContainsRegex("errors parsing \"node\" object.*"
                                       "errors parsing \"locality\" object.*"
                                       "\"region\" field is not a string.*"
                                       "\"zone\" field is not a string.*"
                                       "\"subzone\" field is not a string"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(XdsBootstrapTest, CertificateProvidersElementWrongType) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\""
      "    }"
      "  ],"
      "  \"certificate_providers\": {"
      "    \"plugin\":1"
      "  }"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_string(error),
              ::testing::ContainsRegex(
                  "errors parsing \"certificate_providers\" object.*"
                  "element \"plugin\" is not an object"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(XdsBootstrapTest, CertificateProvidersPluginNameWrongType) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\""
      "    }"
      "  ],"
      "  \"certificate_providers\": {"
      "    \"plugin\": {"
      "      \"plugin_name\":1"
      "    }"
      "  }"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_string(error),
              ::testing::ContainsRegex(
                  "errors parsing \"certificate_providers\" object.*"
                  "errors parsing element \"plugin\".*"
                  "\"plugin_name\" field is not a string"));
  GRPC_ERROR_UNREF(error);
}

class FakeCertificateProviderFactory : public CertificateProviderFactory {
 public:
  class Config : public CertificateProviderFactory::Config {
   public:
    explicit Config(int value) : value_(value) {}

    int value() const { return value_; }

    const char* name() const override { return "fake"; }

   private:
    int value_;
  };

  const char* name() const override { return "fake"; }

  RefCountedPtr<CertificateProviderFactory::Config>
  CreateCertificateProviderConfig(const Json& config_json,
                                  grpc_error** error) override {
    std::vector<grpc_error*> error_list;
    EXPECT_EQ(config_json.type(), Json::Type::OBJECT);
    auto it = config_json.object_value().find("value");
    if (it == config_json.object_value().end()) {
      return MakeRefCounted<FakeCertificateProviderFactory::Config>(0);
    } else if (it->second.type() != Json::Type::NUMBER) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "field:config field:value not of type number");
    } else {
      int value = 0;
      EXPECT_TRUE(absl::SimpleAtoi(it->second.string_value(), &value));
      return MakeRefCounted<FakeCertificateProviderFactory::Config>(value);
    }
    return nullptr;
  }

  RefCountedPtr<grpc_tls_certificate_provider> CreateCertificateProvider(
      RefCountedPtr<CertificateProviderFactory::Config> config) override {
    return nullptr;
  }
};

TEST_F(XdsBootstrapTest, CertificateProvidersFakePluginParsingError) {
  CertificateProviderRegistry::RegisterCertificateProviderFactory(
      absl::make_unique<FakeCertificateProviderFactory>());
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\""
      "    }"
      "  ],"
      "  \"certificate_providers\": {"
      "    \"fake_plugin\": {"
      "      \"plugin_name\": \"fake\","
      "      \"config\": {"
      "        \"value\": \"10\""
      "      }"
      "    }"
      "  }"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_string(error),
              ::testing::ContainsRegex(
                  "errors parsing \"certificate_providers\" object.*"
                  "errors parsing element \"fake_plugin\".*"
                  "field:config field:value not of type number"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(XdsBootstrapTest, CertificateProvidersFakePluginParsingSuccess) {
  CertificateProviderRegistry::RegisterCertificateProviderFactory(
      absl::make_unique<FakeCertificateProviderFactory>());
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\""
      "    }"
      "  ],"
      "  \"certificate_providers\": {"
      "    \"fake_plugin\": {"
      "      \"plugin_name\": \"fake\","
      "      \"config\": {"
      "        \"value\": 10"
      "      }"
      "    }"
      "  }"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const CertificateProviderStore::PluginDefinition& fake_plugin =
      bootstrap.certificate_providers().at("fake_plugin");
  ASSERT_EQ(fake_plugin.plugin_name, "fake");
  ASSERT_STREQ(fake_plugin.config->name(), "fake");
  ASSERT_EQ(static_cast<RefCountedPtr<FakeCertificateProviderFactory::Config>>(
                fake_plugin.config)
                ->value(),
            10);
}

TEST_F(XdsBootstrapTest, CertificateProvidersFakePluginEmptyConfig) {
  CertificateProviderRegistry::RegisterCertificateProviderFactory(
      absl::make_unique<FakeCertificateProviderFactory>());
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\""
      "    }"
      "  ],"
      "  \"certificate_providers\": {"
      "    \"fake_plugin\": {"
      "      \"plugin_name\": \"fake\""
      "    }"
      "  }"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_string(error);
  grpc_core::XdsBootstrap bootstrap(std::move(json), &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const CertificateProviderStore::PluginDefinition& fake_plugin =
      bootstrap.certificate_providers().at("fake_plugin");
  ASSERT_EQ(fake_plugin.plugin_name, "fake");
  ASSERT_STREQ(fake_plugin.config->name(), "fake");
  ASSERT_EQ(static_cast<RefCountedPtr<FakeCertificateProviderFactory::Config>>(
                fake_plugin.config)
                ->value(),
            0);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
