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
#include "absl/strings/str_format.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/slice.h>

#include "src/core/ext/xds/certificate_provider_registry.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/tmpfile.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

class TestType {
 public:
  explicit TestType(bool parse_xds_certificate_providers)
      : parse_xds_certificate_providers_(parse_xds_certificate_providers) {}

  bool parse_xds_certificate_providers() const {
    return parse_xds_certificate_providers_;
  }

  std::string AsString() const {
    return parse_xds_certificate_providers_
               ? "WithCertificateProvidersParsing"
               : "WithoutCertificateProvidersParsing";
  }

 private:
  const bool parse_xds_certificate_providers_;
};

class XdsBootstrapTest : public ::testing::TestWithParam<TestType> {
 public:
  XdsBootstrapTest() {
    if (GetParam().parse_xds_certificate_providers()) {
      gpr_setenv("GRPC_XDS_EXPERIMENTAL_SECURITY_SUPPORT", "true");
    } else {
      gpr_unsetenv("GRPC_XDS_EXPERIMENTAL_SECURITY_SUPPORT");
    }
    grpc_init();
  }

  ~XdsBootstrapTest() override { grpc_shutdown_blocking(); }
};

TEST_P(XdsBootstrapTest, Basic) {
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
      "        },"
      "        {"
      "          \"type\": \"fake\""
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
      "      \"sub_zone\": \"earth\","
      "      \"ignore\": {}"
      "    },"
      "    \"metadata\": {"
      "      \"foo\": 1,"
      "      \"bar\": 2"
      "    },"
      "    \"ignore\": \"whee\""
      "  },"
      "  \"server_listener_resource_name_template\": \"example/resource\","
      "  \"ignore\": {}"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  EXPECT_EQ(bootstrap.server().server_uri, "fake:///lb");
  EXPECT_EQ(bootstrap.server().channel_creds_type, "fake");
  EXPECT_EQ(bootstrap.server().channel_creds_config.type(),
            Json::Type::JSON_NULL);
  ASSERT_NE(bootstrap.node(), nullptr);
  EXPECT_EQ(bootstrap.node()->id, "foo");
  EXPECT_EQ(bootstrap.node()->cluster, "bar");
  EXPECT_EQ(bootstrap.node()->locality_region, "milky_way");
  EXPECT_EQ(bootstrap.node()->locality_zone, "sol_system");
  EXPECT_EQ(bootstrap.node()->locality_sub_zone, "earth");
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
  EXPECT_EQ(bootstrap.server_listener_resource_name_template(),
            "example/resource");
}

TEST_P(XdsBootstrapTest, ValidWithoutNode) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"fake\"}]"
      "    }"
      "  ]"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  EXPECT_EQ(bootstrap.server().server_uri, "fake:///lb");
  EXPECT_EQ(bootstrap.server().channel_creds_type, "fake");
  EXPECT_EQ(bootstrap.node(), nullptr);
}

TEST_P(XdsBootstrapTest, InsecureCreds) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"insecure\"}]"
      "    }"
      "  ]"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  EXPECT_EQ(bootstrap.server().server_uri, "fake:///lb");
  EXPECT_EQ(bootstrap.server().channel_creds_type, "insecure");
  EXPECT_EQ(bootstrap.node(), nullptr);
}

TEST_P(XdsBootstrapTest, GoogleDefaultCreds) {
  // Generate call creds file needed by GoogleDefaultCreds.
  const char token_str[] =
      "{ \"client_id\": \"32555999999.apps.googleusercontent.com\","
      "  \"client_secret\": \"EmssLNjJy1332hD4KFsecret\","
      "  \"refresh_token\": \"1/Blahblasj424jladJDSGNf-u4Sua3HDA2ngjd42\","
      "  \"type\": \"authorized_user\"}";
  char* creds_file_name;
  FILE* creds_file = gpr_tmpfile("xds_bootstrap_test", &creds_file_name);
  ASSERT_NE(creds_file_name, nullptr);
  ASSERT_NE(creds_file, nullptr);
  ASSERT_EQ(fwrite(token_str, 1, sizeof(token_str), creds_file),
            sizeof(token_str));
  fclose(creds_file);
  gpr_setenv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, creds_file_name);
  gpr_free(creds_file_name);
  // Now run test.
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"google_default\"}]"
      "    }"
      "  ]"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  EXPECT_EQ(bootstrap.server().server_uri, "fake:///lb");
  EXPECT_EQ(bootstrap.server().channel_creds_type, "google_default");
  EXPECT_EQ(bootstrap.node(), nullptr);
}

TEST_P(XdsBootstrapTest, MissingChannelCreds) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\""
      "    }"
      "  ]"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex("\"channel_creds\" field not present"));
  GRPC_ERROR_UNREF(error);
}

TEST_P(XdsBootstrapTest, NoKnownChannelCreds) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"unknown\"}]"
      "    }"
      "  ]"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "no known creds type found in \"channel_creds\""));
  GRPC_ERROR_UNREF(error);
}

TEST_P(XdsBootstrapTest, MissingXdsServers) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse("{}", &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex("\"xds_servers\" field not present"));
  GRPC_ERROR_UNREF(error);
}

TEST_P(XdsBootstrapTest, TopFieldsWrongTypes) {
  const char* json_str =
      "{"
      "  \"xds_servers\":1,"
      "  \"node\":1,"
      "  \"server_listener_resource_name_template\":1,"
      "  \"certificate_providers\":1"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex("\"xds_servers\" field is not an array.*"
                                       "\"node\" field is not an object.*"
                                       "\"server_listener_resource_name_"
                                       "template\" field is not a string.*"));
  if (GetParam().parse_xds_certificate_providers()) {
    EXPECT_THAT(grpc_error_std_string(error),
                ::testing::ContainsRegex(
                    "\"certificate_providers\" field is not an object"));
  }
  GRPC_ERROR_UNREF(error);
}

TEST_P(XdsBootstrapTest, XdsServerMissingServerUri) {
  const char* json_str =
      "{"
      "  \"xds_servers\":[{}]"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex("errors parsing \"xds_servers\" array.*"
                                       "errors parsing index 0.*"
                                       "\"server_uri\" field not present"));
  GRPC_ERROR_UNREF(error);
}

TEST_P(XdsBootstrapTest, XdsServerUriAndCredsWrongTypes) {
  const char* json_str =
      "{"
      "  \"xds_servers\":["
      "    {"
      "      \"server_uri\":1,"
      "      \"channel_creds\":1"
      "    }"
      "  ]"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("errors parsing \"xds_servers\" array.*"
                               "errors parsing index 0.*"
                               "\"server_uri\" field is not a string.*"
                               "\"channel_creds\" field is not an array"));
  GRPC_ERROR_UNREF(error);
}

TEST_P(XdsBootstrapTest, ChannelCredsFieldsWrongTypes) {
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("errors parsing \"xds_servers\" array.*"
                               "errors parsing index 0.*"
                               "errors parsing \"channel_creds\" array.*"
                               "errors parsing index 0.*"
                               "\"type\" field is not a string.*"
                               "\"config\" field is not an object"));
  GRPC_ERROR_UNREF(error);
}

TEST_P(XdsBootstrapTest, NodeFieldsWrongTypes) {
  const char* json_str =
      "{"
      "  \"node\":{"
      "    \"id\":0,"
      "    \"cluster\":0,"
      "    \"locality\":0,"
      "    \"metadata\":0"
      "  }"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex("errors parsing \"node\" object.*"
                                       "\"id\" field is not a string.*"
                                       "\"cluster\" field is not a string.*"
                                       "\"locality\" field is not an object.*"
                                       "\"metadata\" field is not an object"));
  GRPC_ERROR_UNREF(error);
}

TEST_P(XdsBootstrapTest, LocalityFieldsWrongType) {
  const char* json_str =
      "{"
      "  \"node\":{"
      "    \"locality\":{"
      "      \"region\":0,"
      "      \"zone\":0,"
      "      \"sub_zone\":0"
      "    }"
      "  }"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex("errors parsing \"node\" object.*"
                                       "errors parsing \"locality\" object.*"
                                       "\"region\" field is not a string.*"
                                       "\"zone\" field is not a string.*"
                                       "\"sub_zone\" field is not a string"));
  GRPC_ERROR_UNREF(error);
}

TEST_P(XdsBootstrapTest, CertificateProvidersElementWrongType) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"fake\"}]"
      "    }"
      "  ],"
      "  \"certificate_providers\": {"
      "    \"plugin\":1"
      "  }"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  if (GetParam().parse_xds_certificate_providers()) {
    EXPECT_THAT(grpc_error_std_string(error),
                ::testing::ContainsRegex(
                    "errors parsing \"certificate_providers\" object.*"
                    "element \"plugin\" is not an object"));
  } else {
    EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  }
  GRPC_ERROR_UNREF(error);
}

TEST_P(XdsBootstrapTest, CertificateProvidersPluginNameWrongType) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"fake\"}]"
      "    }"
      "  ],"
      "  \"certificate_providers\": {"
      "    \"plugin\": {"
      "      \"plugin_name\":1"
      "    }"
      "  }"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  if (GetParam().parse_xds_certificate_providers()) {
    EXPECT_THAT(grpc_error_std_string(error),
                ::testing::ContainsRegex(
                    "errors parsing \"certificate_providers\" object.*"
                    "errors parsing element \"plugin\".*"
                    "\"plugin_name\" field is not a string"));
  } else {
    EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  }
  GRPC_ERROR_UNREF(error);
}

class FakeCertificateProviderFactory : public CertificateProviderFactory {
 public:
  class Config : public CertificateProviderFactory::Config {
   public:
    explicit Config(int value) : value_(value) {}

    int value() const { return value_; }

    const char* name() const override { return "fake"; }

    std::string ToString() const override {
      return absl::StrFormat(
          "{\n"
          "  value=%d"
          "}",
          value_);
    }

   private:
    int value_;
  };

  const char* name() const override { return "fake"; }

  RefCountedPtr<CertificateProviderFactory::Config>
  CreateCertificateProviderConfig(const Json& config_json,
                                  grpc_error_handle* error) override {
    std::vector<grpc_error_handle> error_list;
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
      RefCountedPtr<CertificateProviderFactory::Config> /*config*/) override {
    return nullptr;
  }
};

TEST_P(XdsBootstrapTest, CertificateProvidersFakePluginParsingError) {
  CertificateProviderRegistry::RegisterCertificateProviderFactory(
      absl::make_unique<FakeCertificateProviderFactory>());
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"fake\"}]"
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  if (GetParam().parse_xds_certificate_providers()) {
    EXPECT_THAT(grpc_error_std_string(error),
                ::testing::ContainsRegex(
                    "errors parsing \"certificate_providers\" object.*"
                    "errors parsing element \"fake_plugin\".*"
                    "field:config field:value not of type number"));
  } else {
    EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  }
  GRPC_ERROR_UNREF(error);
}

TEST_P(XdsBootstrapTest, CertificateProvidersFakePluginParsingSuccess) {
  CertificateProviderRegistry::RegisterCertificateProviderFactory(
      absl::make_unique<FakeCertificateProviderFactory>());
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"fake\"}]"
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE) << grpc_error_std_string(error);
  if (GetParam().parse_xds_certificate_providers()) {
    const CertificateProviderStore::PluginDefinition& fake_plugin =
        bootstrap.certificate_providers().at("fake_plugin");
    ASSERT_EQ(fake_plugin.plugin_name, "fake");
    ASSERT_STREQ(fake_plugin.config->name(), "fake");
    ASSERT_EQ(
        static_cast<RefCountedPtr<FakeCertificateProviderFactory::Config>>(
            fake_plugin.config)
            ->value(),
        10);
  } else {
    EXPECT_TRUE(bootstrap.certificate_providers().empty());
  }
}

TEST_P(XdsBootstrapTest, CertificateProvidersFakePluginEmptyConfig) {
  CertificateProviderRegistry::RegisterCertificateProviderFactory(
      absl::make_unique<FakeCertificateProviderFactory>());
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"fake\"}]"
      "    }"
      "  ],"
      "  \"certificate_providers\": {"
      "    \"fake_plugin\": {"
      "      \"plugin_name\": \"fake\""
      "    }"
      "  }"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(json_str, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  XdsBootstrap bootstrap(std::move(json), &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE) << grpc_error_std_string(error);
  if (GetParam().parse_xds_certificate_providers()) {
    const CertificateProviderStore::PluginDefinition& fake_plugin =
        bootstrap.certificate_providers().at("fake_plugin");
    ASSERT_EQ(fake_plugin.plugin_name, "fake");
    ASSERT_STREQ(fake_plugin.config->name(), "fake");
    ASSERT_EQ(
        static_cast<RefCountedPtr<FakeCertificateProviderFactory::Config>>(
            fake_plugin.config)
            ->value(),
        0);
  } else {
    EXPECT_TRUE(bootstrap.certificate_providers().empty());
  }
}

std::string TestTypeName(const ::testing::TestParamInfo<TestType>& info) {
  return info.param.AsString();
}

INSTANTIATE_TEST_SUITE_P(XdsBootstrap, XdsBootstrapTest,
                         ::testing::Values(TestType(false), TestType(true)),
                         &TestTypeName);

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
