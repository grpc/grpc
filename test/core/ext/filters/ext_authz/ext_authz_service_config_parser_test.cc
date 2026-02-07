
#include "src/core/ext/filters/ext_authz/ext_authz_service_config_parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/config/core_configuration.h"
#include "src/core/service_config/service_config_impl.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class ExtAuthzServiceConfigParsingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    parser_index_ =
        CoreConfiguration::Get().service_config_parser().GetParserIndex(
            "ext_authz");
  }

  size_t parser_index_;
};

TEST_F(ExtAuthzServiceConfigParsingTest, ParseValidConfig) {
  const char* test_json =
      "{\n"
      "  \"ext_authz\": [\n"
      "    {\n"
      "      \"ext_authz\": {\n"
      "        \"deny_at_disable\": true,\n"
      "        \"failure_mode_allow\": true,\n"
      "        \"status_on_error\": 404,\n"
      "        \"failure_mode_allow_header_add\": true,\n"
      "        \"include_peer_certificate\": true,\n"
      "        \"xds_grpc_service\": {\n"
      "          \"initial_metadata\": [\n"
      "            {\n"
      "              \"key\": \"foo\",\n"
      "              \"value\": \"bar\"\n"
      "            },\n"
      "            {\n"
      "              \"key\": \"foo\",\n"
      "              \"value\": \"bar\"\n"
      "            }\n"
      "          ],\n"
      "          \"server_target\": {\n"
      "            \"call_creds\": [\n"
      "              { \"type\": \"jwt_token_file\", \"config\": "
      "{\"jwt_token_file\": \"/tmp/token\"} },\n"
      "              { \"type\": \"jwt_token_file\", \"config\": "
      "{\"jwt_token_file\": \"/tmp/token\"} }\n"
      "            ],\n"
      "            \"channel_creds\": [\n"
      "              {\n"
      "                \"config\": {},\n"
      "                \"type\": \"insecure\"\n"
      "              }\n"
      "            ],\n"
      "            \"server_uri\": \"dns:server.example.com\"\n"
      "          },\n"
      "          \"timeout\": \"0.000000000s\"\n"
      "        },\n"
      "        \"filter_enabled\": {\n"
      "          \"denominator\": 10000,\n"
      "          \"numerator\": 100\n"
      "        }\n"
      "      },\n"
      "      \"filter_instance_name\": \"\"\n"
      "    }\n"
      "  ]\n"
      "}";

  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.status().ok()) << service_config.status();
  const auto* parsed_config = static_cast<ExtAuthzParsedConfig*>(
      (*service_config)->GetGlobalParsedConfig(parser_index_));
  ASSERT_NE(parsed_config, nullptr);
  const auto* config = parsed_config->GetConfig(0);
  ASSERT_NE(config, nullptr);
  EXPECT_EQ(config->filter_instance_name, "");
  ASSERT_NE(config->ext_authz.xds_grpc_service.server_target, nullptr);
  EXPECT_EQ(config->ext_authz.xds_grpc_service.server_target->server_uri(),
            "dns:server.example.com");
  EXPECT_EQ(config->ext_authz.xds_grpc_service.initial_metadata.size(), 2);
  EXPECT_EQ(config->ext_authz.xds_grpc_service.initial_metadata[0].first,
            "foo");
  EXPECT_EQ(config->ext_authz.xds_grpc_service.initial_metadata[0].second,
            "bar");
  EXPECT_EQ(config->ext_authz.xds_grpc_service.initial_metadata[1].first,
            "foo");
  EXPECT_EQ(config->ext_authz.xds_grpc_service.initial_metadata[1].second,
            "bar");
  EXPECT_EQ(config->ext_authz.xds_grpc_service.timeout, Duration::Zero());
  ASSERT_NE(
      config->ext_authz.xds_grpc_service.server_target->channel_creds_config(),
      nullptr);
  EXPECT_EQ(
      config->ext_authz.xds_grpc_service.server_target->channel_creds_config()
          ->type(),
      "insecure");
  ASSERT_EQ(
      config->ext_authz.xds_grpc_service.server_target->call_creds_configs()
          .size(),
      2);
  EXPECT_EQ(
      config->ext_authz.xds_grpc_service.server_target->call_creds_configs()[0]
          ->type(),
      "jwt_token_file");
  EXPECT_EQ(
      config->ext_authz.xds_grpc_service.server_target->call_creds_configs()[1]
          ->type(),
      "jwt_token_file");
  EXPECT_EQ(config->ext_authz.filter_enabled->numerator, 100);
  EXPECT_EQ(config->ext_authz.filter_enabled->denominator, 10000);
  EXPECT_EQ(config->ext_authz.deny_at_disable, true);
  EXPECT_EQ(config->ext_authz.include_peer_certificate, true);
  EXPECT_EQ(config->ext_authz.status_on_error, GRPC_STATUS_UNIMPLEMENTED);
  
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
