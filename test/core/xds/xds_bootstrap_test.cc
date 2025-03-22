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

#include "src/core/xds/xds_client/xds_bootstrap.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/support/alloc.h>
#include <stdio.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/channel_creds_registry.h"
#include "src/core/credentials/transport/tls/certificate_provider_factory.h"
#include "src/core/util/env.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/tmpfile.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/certificate_provider_store.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

MATCHER_P5(EqXdsServer, name, creds_config_type, ignore_resource_deletion,
           fail_on_data_errors, trusted_xds_server, "equals XdsServer") {
  auto* server = static_cast<const GrpcXdsServer*>(arg);
  if (!::testing::ExplainMatchResult(::testing::Ne(nullptr), server,
                                     result_listener)) {
    return false;
  }
  bool ok = ::testing::ExplainMatchResult(name, server->target()->server_uri(),
                                          result_listener);
  ok &=
      ::testing::ExplainMatchResult(server->IgnoreResourceDeletion(),
                                    ignore_resource_deletion, result_listener);
  ok &= ::testing::ExplainMatchResult(server->FailOnDataErrors(),
                                      fail_on_data_errors, result_listener);
  ok &= ::testing::ExplainMatchResult(server->TrustedXdsServer(),
                                      trusted_xds_server, result_listener);
  auto& server_target = DownCast<const GrpcXdsServerTarget&>(*server->target());
  auto creds_config = server_target.channel_creds_config();
  if (!::testing::ExplainMatchResult(::testing::Ne(nullptr), creds_config,
                                     result_listener)) {
    return false;
  }
  ok &= ::testing::ExplainMatchResult(creds_config_type, creds_config->type(),
                                      result_listener);
  return ok;
}

TEST(XdsBootstrapTest, Basic) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb1\","
      "      \"channel_creds\": ["
      "        {"
      "          \"type\": \"fake\","
      "          \"ignore\": 0"
      "        }"
      "      ],"
      "      \"ignore\": 0"
      "    }"
      "  ],"
      "  \"authorities\": {"
      "    \"xds.example.com\": {"
      "      \"client_listener_resource_name_template\": "
      "\"xdstp://xds.example.com/envoy.config.listener.v3.Listener/grpc/server/"
      "%s\","
      "      \"xds_servers\": ["
      "        {"
      "          \"server_uri\": \"fake:///xds_server\","
      "          \"channel_creds\": ["
      "            {"
      "              \"type\": \"fake\""
      "            }"
      "          ],"
      "          \"server_features\": ["
      "            \"xds_v3\","
      "            \"ignore_resource_deletion\""
      "          ]"
      "        }"
      "      ]"
      "    },"
      "    \"xds.example2.com\": {"
      "      \"client_listener_resource_name_template\": "
      "\"xdstp://xds.example2.com/envoy.config.listener.v3.Listener/grpc/"
      "server/%s\","
      "      \"xds_servers\": ["
      "        {"
      "          \"server_uri\": \"fake:///xds_server3\","
      "          \"channel_creds\": ["
      "            {"
      "              \"type\": \"fake\""
      "            }"
      "          ],"
      "          \"server_features\": ["
      "            \"trusted_xds_server\","
      "            \"fail_on_data_errors\""
      "          ]"
      "        }"
      "      ]"
      "    }"
      "  },"
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
  auto bootstrap_or = GrpcXdsBootstrap::Create(json_str);
  ASSERT_TRUE(bootstrap_or.ok()) << bootstrap_or.status();
  auto bootstrap = std::move(*bootstrap_or);
  EXPECT_THAT(bootstrap->servers(),
              ::testing::ElementsAre(
                  EqXdsServer("fake:///lb1", "fake", false, false, false)));
  EXPECT_EQ(bootstrap->authorities().size(), 2);
  auto* authority = static_cast<const GrpcXdsBootstrap::GrpcAuthority*>(
      bootstrap->LookupAuthority("xds.example.com"));
  ASSERT_NE(authority, nullptr);
  EXPECT_EQ(authority->client_listener_resource_name_template(),
            "xdstp://xds.example.com/envoy.config.listener.v3.Listener/grpc/"
            "server/%s");
  EXPECT_THAT(authority->servers(),
              ::testing::ElementsAre(EqXdsServer("fake:///xds_server", "fake",
                                                 true, false, false)));
  authority = static_cast<const GrpcXdsBootstrap::GrpcAuthority*>(
      bootstrap->LookupAuthority("xds.example2.com"));
  ASSERT_NE(authority, nullptr);
  EXPECT_EQ(authority->client_listener_resource_name_template(),
            "xdstp://xds.example2.com/envoy.config.listener.v3.Listener/grpc/"
            "server/%s");
  EXPECT_THAT(authority->servers(),
              ::testing::ElementsAre(EqXdsServer("fake:///xds_server3", "fake",
                                                 false, true, true)));
  ASSERT_NE(bootstrap->node(), nullptr);
  EXPECT_EQ(bootstrap->node()->id(), "foo");
  EXPECT_EQ(bootstrap->node()->cluster(), "bar");
  EXPECT_EQ(bootstrap->node()->locality_region(), "milky_way");
  EXPECT_EQ(bootstrap->node()->locality_zone(), "sol_system");
  EXPECT_EQ(bootstrap->node()->locality_sub_zone(), "earth");
  EXPECT_THAT(bootstrap->node()->metadata(),
              ::testing::ElementsAre(
                  ::testing::Pair(
                      ::testing::Eq("bar"),
                      ::testing::AllOf(
                          ::testing::Property(&Json::type, Json::Type::kNumber),
                          ::testing::Property(&Json::string, "2"))),
                  ::testing::Pair(
                      ::testing::Eq("foo"),
                      ::testing::AllOf(
                          ::testing::Property(&Json::type, Json::Type::kNumber),
                          ::testing::Property(&Json::string, "1")))));
  EXPECT_EQ(bootstrap->server_listener_resource_name_template(),
            "example/resource");
}

TEST(XdsBootstrapTest, ValidWithoutNode) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"fake\"}]"
      "    }"
      "  ]"
      "}";
  auto bootstrap_or = GrpcXdsBootstrap::Create(json_str);
  ASSERT_TRUE(bootstrap_or.ok()) << bootstrap_or.status();
  auto bootstrap = std::move(*bootstrap_or);
  EXPECT_THAT(bootstrap->servers(),
              ::testing::ElementsAre(
                  EqXdsServer("fake:///lb", "fake", false, false, false)));
  EXPECT_EQ(bootstrap->node(), nullptr);
}

TEST(XdsBootstrapTest, InsecureCreds) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"insecure\"}]"
      "    }"
      "  ]"
      "}";
  auto bootstrap_or = GrpcXdsBootstrap::Create(json_str);
  ASSERT_TRUE(bootstrap_or.ok()) << bootstrap_or.status();
  auto bootstrap = std::move(*bootstrap_or);
  EXPECT_THAT(bootstrap->servers(),
              ::testing::ElementsAre(
                  EqXdsServer("fake:///lb", "insecure", false, false, false)));
  EXPECT_EQ(bootstrap->node(), nullptr);
}

TEST(XdsBootstrapTest, GoogleDefaultCreds) {
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
  SetEnv(GRPC_GOOGLE_CREDENTIALS_ENV_VAR, creds_file_name);
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
  auto bootstrap_or = GrpcXdsBootstrap::Create(json_str);
  ASSERT_TRUE(bootstrap_or.ok()) << bootstrap_or.status();
  auto bootstrap = std::move(*bootstrap_or);
  EXPECT_THAT(bootstrap->servers(),
              ::testing::ElementsAre(EqXdsServer("fake:///lb", "google_default",
                                                 false, false, false)));
  EXPECT_EQ(bootstrap->node(), nullptr);
}

TEST(XdsBootstrapTest, MissingChannelCreds) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\""
      "    }"
      "  ]"
      "}";
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(bootstrap.status().message(),
            "errors validating JSON: ["
            "field:xds_servers[0].channel_creds error:field not present]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, NoKnownChannelCreds) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"unknown\"}]"
      "    }"
      "  ]"
      "}";
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(bootstrap.status().message(),
            "errors validating JSON: ["
            "field:xds_servers[0].channel_creds "
            "error:no known creds type found]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, MultipleCreds) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"unknown\"}, {\"type\": "
      "\"fake\"}, {\"type\": \"insecure\"}]"
      "    }"
      "  ]"
      "}";
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  ASSERT_TRUE(bootstrap.ok()) << bootstrap.status();
  EXPECT_THAT((*bootstrap)->servers(),
              ::testing::ElementsAre(
                  EqXdsServer("fake:///lb", "fake", false, false, false)));
  EXPECT_EQ((*bootstrap)->node(), nullptr);
}

TEST(XdsBootstrapTest, MissingXdsServers) {
  auto bootstrap = GrpcXdsBootstrap::Create("{}");
  EXPECT_EQ(
      bootstrap.status().message(),
      "errors validating JSON: [field:xds_servers error:field not present]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, EmptyXdsServers) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "  ]"
      "}";
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(
      bootstrap.status().message(),
      "errors validating JSON: [field:xds_servers error:must be non-empty]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, TopFieldsWrongTypes) {
  const char* json_str =
      "{"
      "  \"xds_servers\":1,"
      "  \"node\":1,"
      "  \"server_listener_resource_name_template\":1,"
      "  \"certificate_providers\":1"
      "}";
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(
      bootstrap.status().message(),
      "errors validating JSON: ["
      "field:certificate_providers error:is not an object; "
      "field:node error:is not an object; "
      "field:server_listener_resource_name_template error:is not a string; "
      "field:xds_servers error:is not an array]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, XdsServerMissingFields) {
  const char* json_str =
      "{"
      "  \"xds_servers\":[{}]"
      "}";
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(bootstrap.status().message(),
            "errors validating JSON: ["
            "field:xds_servers[0].channel_creds error:field not present; "
            "field:xds_servers[0].server_uri error:field not present]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, XdsServerUriAndCredsWrongTypes) {
  const char* json_str =
      "{"
      "  \"xds_servers\":["
      "    {"
      "      \"server_uri\":1,"
      "      \"channel_creds\":1"
      "    }"
      "  ]"
      "}";
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(bootstrap.status().message(),
            "errors validating JSON: ["
            "field:xds_servers[0].channel_creds error:is not an array; "
            "field:xds_servers[0].server_uri error:is not a string]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, ChannelCredsFieldsWrongTypes) {
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
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(
      bootstrap.status().message(),
      "errors validating JSON: ["
      "field:xds_servers[0].channel_creds[0].config error:is not an object; "
      "field:xds_servers[0].channel_creds[0].type error:is not a string]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, NodeFieldsWrongTypes) {
  const char* json_str =
      "{"
      "  \"node\":{"
      "    \"id\":0,"
      "    \"cluster\":0,"
      "    \"locality\":0,"
      "    \"metadata\":0"
      "  }"
      "}";
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(bootstrap.status().message(),
            "errors validating JSON: ["
            "field:node.cluster error:is not a string; "
            "field:node.id error:is not a string; "
            "field:node.locality error:is not an object; "
            "field:node.metadata error:is not an object; "
            "field:xds_servers error:field not present]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, LocalityFieldsWrongType) {
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
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(bootstrap.status().message(),
            "errors validating JSON: ["
            "field:node.locality.region error:is not a string; "
            "field:node.locality.sub_zone error:is not a string; "
            "field:node.locality.zone error:is not a string; "
            "field:xds_servers error:field not present]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, CertificateProvidersElementWrongType) {
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
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(bootstrap.status().message(),
            "errors validating JSON: ["
            "field:certificate_providers[\"plugin\"] error:is not an object]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, CertificateProvidersPluginNameWrongType) {
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
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(bootstrap.status().message(),
            "errors validating JSON: ["
            "field:certificate_providers[\"plugin\"].plugin_name error:"
            "is not a string]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, CertificateProvidersUnrecognizedPluginName) {
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
      "      \"plugin_name\":\"unknown\""
      "    }"
      "  }"
      "}";
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(bootstrap.status().message(),
            "errors validating JSON: ["
            "field:certificate_providers[\"plugin\"].plugin_name error:"
            "Unrecognized plugin name: unknown]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, AuthorityXdsServerInvalidResourceTemplate) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"fake\"}]"
      "    }"
      "  ],"
      "  \"authorities\": {"
      "    \"xds.example.com\": {"
      "      \"client_listener_resource_name_template\": "
      "\"xds://xds.example.com/envoy.config.listener.v3.Listener/grpc/server/"
      "%s\","
      "      \"xds_servers\": ["
      "        {"
      "          \"server_uri\": \"fake:///xds_server\","
      "          \"channel_creds\": ["
      "            {"
      "              \"type\": \"fake\""
      "            }"
      "          ],"
      "          \"server_features\": [\"xds_v3\"]"
      "        }"
      "      ]"
      "    }"
      "  }"
      "}";
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(bootstrap.status().message(),
            "errors validating JSON: ["
            "field:authorities[\"xds.example.com\"]"
            ".client_listener_resource_name_template error:"
            "field must begin with \"xdstp://xds.example.com/\"]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, AuthorityXdsServerMissingServerUri) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb\","
      "      \"channel_creds\": [{\"type\": \"fake\"}]"
      "    }"
      "  ],"
      "  \"authorities\": {"
      "    \"xds.example.com\": {"
      "      \"client_listener_resource_name_template\": "
      "\"xdstp://xds.example.com/envoy.config.listener.v3.Listener/grpc/server/"
      "%s\","
      "      \"xds_servers\":[{}]"
      "    }"
      "  }"
      "}";
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(
      bootstrap.status().message(),
      "errors validating JSON: ["
      "field:authorities[\"xds.example.com\"].xds_servers[0].channel_creds "
      "error:field not present; "
      "field:authorities[\"xds.example.com\"].xds_servers[0].server_uri "
      "error:field not present]")
      << bootstrap.status();
}

class FakeCertificateProviderFactory : public CertificateProviderFactory {
 public:
  class Config : public CertificateProviderFactory::Config {
   public:
    int value() const { return value_; }

    absl::string_view name() const override { return "fake"; }

    std::string ToString() const override {
      return absl::StrFormat(
          "{\n"
          "  value=%d"
          "}",
          value_);
    }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader = JsonObjectLoader<Config>()
                                      .OptionalField("value", &Config::value_)
                                      .Finish();
      return loader;
    }

   private:
    int value_;
  };

  absl::string_view name() const override { return "fake"; }

  RefCountedPtr<CertificateProviderFactory::Config>
  CreateCertificateProviderConfig(const Json& config_json, const JsonArgs& args,
                                  ValidationErrors* errors) override {
    return LoadFromJson<RefCountedPtr<Config>>(config_json, args, errors);
  }

  RefCountedPtr<grpc_tls_certificate_provider> CreateCertificateProvider(
      RefCountedPtr<CertificateProviderFactory::Config> /*config*/) override {
    return nullptr;
  }
};

TEST(XdsBootstrapTest, CertificateProvidersFakePluginParsingError) {
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
      "        \"value\": []"
      "      }"
      "    }"
      "  }"
      "}";
  auto bootstrap = GrpcXdsBootstrap::Create(json_str);
  EXPECT_EQ(bootstrap.status().message(),
            "errors validating JSON: ["
            "field:certificate_providers[\"fake_plugin\"].config.value "
            "error:is not a number]")
      << bootstrap.status();
}

TEST(XdsBootstrapTest, CertificateProvidersFakePluginParsingSuccess) {
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
  auto bootstrap_or = GrpcXdsBootstrap::Create(json_str);
  ASSERT_TRUE(bootstrap_or.ok()) << bootstrap_or.status();
  auto bootstrap = std::move(*bootstrap_or);
  const CertificateProviderStore::PluginDefinition& fake_plugin =
      bootstrap->certificate_providers().at("fake_plugin");
  ASSERT_EQ(fake_plugin.plugin_name, "fake");
  ASSERT_EQ(fake_plugin.config->name(), "fake");
  auto* config = static_cast<FakeCertificateProviderFactory::Config*>(
      fake_plugin.config.get());
  ASSERT_EQ(config->value(), 10);
}

TEST(XdsBootstrapTest, CertificateProvidersFakePluginEmptyConfig) {
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
  auto bootstrap_or = GrpcXdsBootstrap::Create(json_str);
  ASSERT_TRUE(bootstrap_or.ok()) << bootstrap_or.status();
  auto bootstrap = std::move(*bootstrap_or);
  const CertificateProviderStore::PluginDefinition& fake_plugin =
      bootstrap->certificate_providers().at("fake_plugin");
  ASSERT_EQ(fake_plugin.plugin_name, "fake");
  ASSERT_EQ(fake_plugin.config->name(), "fake");
  auto* config = static_cast<FakeCertificateProviderFactory::Config*>(
      fake_plugin.config.get());
  ASSERT_EQ(config->value(), 0);
}

TEST(XdsBootstrapTest, MultipleXdsServers) {
  const char* json_str =
      "{"
      "  \"xds_servers\": ["
      "    {"
      "      \"server_uri\": \"fake:///lb1\","
      "      \"channel_creds\": ["
      "        {"
      "          \"type\": \"fake\","
      "          \"ignore\": 0"
      "        }"
      "      ],"
      "      \"ignore\": 0"
      "    },"
      "    {"
      "      \"server_uri\": \"fake:///lb2\","
      "      \"channel_creds\": ["
      "        {"
      "          \"type\": \"fake\","
      "          \"ignore\": 0"
      "        }"
      "      ],"
      "      \"ignore\": 0"
      "    }"
      "  ],"
      "  \"authorities\": {"
      "    \"xds.example.com\": {"
      "      \"client_listener_resource_name_template\": "
      "\"xdstp://xds.example.com/envoy.config.listener.v3.Listener/grpc/server/"
      "%s\","
      "      \"xds_servers\": ["
      "        {"
      "          \"server_uri\": \"fake:///xds_server\","
      "          \"channel_creds\": ["
      "            {"
      "              \"type\": \"fake\""
      "            }"
      "          ],"
      "          \"server_features\": [\"xds_v3\"]"
      "        },"
      "        {"
      "          \"server_uri\": \"fake:///xds_server2\","
      "          \"channel_creds\": ["
      "            {"
      "              \"type\": \"fake\""
      "            }"
      "          ],"
      "          \"server_features\": [\"xds_v3\"]"
      "        }"
      "      ]"
      "    }"
      "  },"
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
  auto bootstrap_or = GrpcXdsBootstrap::Create(json_str);
  ASSERT_TRUE(bootstrap_or.ok()) << bootstrap_or.status();
  auto bootstrap = std::move(*bootstrap_or);
  EXPECT_THAT(bootstrap->servers(),
              ::testing::ElementsAre(
                  EqXdsServer("fake:///lb1", "fake", false, false, false),
                  EqXdsServer("fake:///lb2", "fake", false, false, false)));
  auto* authority = static_cast<const GrpcXdsBootstrap::GrpcAuthority*>(
      bootstrap->LookupAuthority("xds.example.com"));
  ASSERT_NE(authority, nullptr);
  EXPECT_THAT(
      authority->servers(),
      ::testing::ElementsAre(
          EqXdsServer("fake:///xds_server", "fake", false, false, false),
          EqXdsServer("fake:///xds_server2", "fake", false, false, false)));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_core::CoreConfiguration::RegisterBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        builder->certificate_provider_registry()
            ->RegisterCertificateProviderFactory(
                std::make_unique<
                    grpc_core::testing::FakeCertificateProviderFactory>());
      });
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
