/*
 *
 * Copyright 2019 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/service_config/service_config.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>

#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/ext/filters/client_channel/retry_service_config.h"
#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "src/core/lib/service_config/service_config_parser.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

//
// ServiceConfig tests
//

// Set this channel arg to true to disable parsing.
#define GRPC_ARG_DISABLE_PARSING "disable_parsing"

// A regular expression to enter referenced or child errors.
#define CHILD_ERROR_TAG ".*children.*"

class TestParsedConfig1 : public ServiceConfigParser::ParsedConfig {
 public:
  explicit TestParsedConfig1(int value) : value_(value) {}

  int value() const { return value_; }

 private:
  int value_;
};

class TestParser1 : public ServiceConfigParser::Parser {
 public:
  absl::string_view name() const override { return "test_parser_1"; }

  absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
  ParseGlobalParams(const ChannelArgs& args, const Json& json) override {
    if (args.GetBool(GRPC_ARG_DISABLE_PARSING).value_or(false)) {
      return nullptr;
    }
    auto it = json.object_value().find("global_param");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::NUMBER) {
        return absl::InvalidArgumentError(InvalidTypeErrorMessage());
      }
      int value = gpr_parse_nonnegative_int(it->second.string_value().c_str());
      if (value == -1) {
        return absl::InvalidArgumentError(InvalidValueErrorMessage());
      }
      return absl::make_unique<TestParsedConfig1>(value);
    }
    return nullptr;
  }

  static const char* InvalidTypeErrorMessage() {
    return "global_param value type should be a number";
  }

  static const char* InvalidValueErrorMessage() {
    return "global_param value type should be non-negative";
  }
};

class TestParser2 : public ServiceConfigParser::Parser {
 public:
  absl::string_view name() const override { return "test_parser_2"; }

  absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
  ParsePerMethodParams(const ChannelArgs& args, const Json& json) override {
    if (args.GetBool(GRPC_ARG_DISABLE_PARSING).value_or(false)) {
      return nullptr;
    }
    auto it = json.object_value().find("method_param");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::NUMBER) {
        return absl::InvalidArgumentError(InvalidTypeErrorMessage());
      }
      int value = gpr_parse_nonnegative_int(it->second.string_value().c_str());
      if (value == -1) {
        return absl::InvalidArgumentError(InvalidValueErrorMessage());
      }
      return absl::make_unique<TestParsedConfig1>(value);
    }
    return nullptr;
  }

  static const char* InvalidTypeErrorMessage() {
    return "method_param value type should be a number";
  }

  static const char* InvalidValueErrorMessage() {
    return "method_param value type should be non-negative";
  }
};

// This parser always adds errors
class ErrorParser : public ServiceConfigParser::Parser {
 public:
  explicit ErrorParser(absl::string_view name) : name_(name) {}

  absl::string_view name() const override { return name_; }

  absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
  ParsePerMethodParams(const ChannelArgs& /*arg*/,
                       const Json& /*json*/) override {
    return absl::InvalidArgumentError(MethodError());
  }

  absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
  ParseGlobalParams(const ChannelArgs& /*arg*/, const Json& /*json*/) override {
    return absl::InvalidArgumentError(GlobalError());
  }

  static const char* MethodError() { return "ErrorParser : methodError"; }

  static const char* GlobalError() { return "ErrorParser : globalError"; }

 private:
  absl::string_view name_;
};

class ServiceConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    builder_ = std::make_unique<CoreConfiguration::WithSubstituteBuilder>(
        [](CoreConfiguration::Builder* builder) {
          builder->service_config_parser()->RegisterParser(
              absl::make_unique<TestParser1>());
          builder->service_config_parser()->RegisterParser(
              absl::make_unique<TestParser2>());
        });
    EXPECT_EQ(CoreConfiguration::Get().service_config_parser().GetParserIndex(
                  "test_parser_1"),
              0);
    EXPECT_EQ(CoreConfiguration::Get().service_config_parser().GetParserIndex(
                  "test_parser_2"),
              1);
  }

 private:
  std::unique_ptr<CoreConfiguration::WithSubstituteBuilder> builder_;
};

TEST_F(ServiceConfigTest, ErrorCheck1) {
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), "");
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex("JSON parse error"));
}

TEST_F(ServiceConfigTest, BasicTest1) {
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), "{}");
  ASSERT_TRUE(service_config.ok()) << service_config.status();
}

TEST_F(ServiceConfigTest, SkipMethodConfigWithNoNameOrEmptyName) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"method_param\":1},"
      "  {\"name\":[], \"method_param\":1},"
      "  {\"name\":[{\"service\":\"TestServ\"}], \"method_param\":2}"
      "]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  auto vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_EQ(vector_ptr->size(), 2UL);
  auto parsed_config = ((*vector_ptr)[1]).get();
  EXPECT_EQ(static_cast<TestParsedConfig1*>(parsed_config)->value(), 2);
}

TEST_F(ServiceConfigTest, ErrorDuplicateMethodConfigNames) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"name\":[{\"service\":\"TestServ\"}]},"
      "  {\"name\":[{\"service\":\"TestServ\"}]}"
      "]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "Service config parsing errors: ["
            "errors parsing methodConfig: ["
            "index 1: ["
            "field:name error:multiple method configs with same name]]]");
}

TEST_F(ServiceConfigTest, ErrorDuplicateMethodConfigNamesWithNullMethod) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"name\":[{\"service\":\"TestServ\",\"method\":null}]},"
      "  {\"name\":[{\"service\":\"TestServ\"}]}"
      "]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "Service config parsing errors: ["
            "errors parsing methodConfig: ["
            "index 1: ["
            "field:name error:multiple method configs with same name]]]");
}

TEST_F(ServiceConfigTest, ErrorDuplicateMethodConfigNamesWithEmptyMethod) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"name\":[{\"service\":\"TestServ\",\"method\":\"\"}]},"
      "  {\"name\":[{\"service\":\"TestServ\"}]}"
      "]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "Service config parsing errors: ["
            "errors parsing methodConfig: ["
            "index 1: ["
            "field:name error:multiple method configs with same name]]]");
}

TEST_F(ServiceConfigTest, ErrorDuplicateDefaultMethodConfigs) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"name\":[{}]},"
      "  {\"name\":[{}]}"
      "]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "Service config parsing errors: ["
            "errors parsing methodConfig: ["
            "index 1: ["
            "field:name error:multiple default method configs]]]");
}

TEST_F(ServiceConfigTest, ErrorDuplicateDefaultMethodConfigsWithNullService) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"name\":[{\"service\":null}]},"
      "  {\"name\":[{}]}"
      "]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "Service config parsing errors: ["
            "errors parsing methodConfig: ["
            "index 1: ["
            "field:name error:multiple default method configs]]]");
}

TEST_F(ServiceConfigTest, ErrorDuplicateDefaultMethodConfigsWithEmptyService) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"name\":[{\"service\":\"\"}]},"
      "  {\"name\":[{}]}"
      "]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "Service config parsing errors: ["
            "errors parsing methodConfig: ["
            "index 1: ["
            "field:name error:multiple default method configs]]]");
}

TEST_F(ServiceConfigTest, ValidMethodConfig) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}]}]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
}

TEST_F(ServiceConfigTest, Parser1BasicTest1) {
  const char* test_json = "{\"global_param\":5}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  EXPECT_EQ((static_cast<TestParsedConfig1*>(
                 (*service_config)->GetGlobalParsedConfig(0)))
                ->value(),
            5);
  EXPECT_EQ((*service_config)
                ->GetMethodParsedConfigVector(
                    grpc_slice_from_static_string("/TestServ/TestMethod")),
            nullptr);
}

TEST_F(ServiceConfigTest, Parser1BasicTest2) {
  const char* test_json = "{\"global_param\":1000}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  EXPECT_EQ((static_cast<TestParsedConfig1*>(
                 (*service_config)->GetGlobalParsedConfig(0)))
                ->value(),
            1000);
}

TEST_F(ServiceConfigTest, Parser1DisabledViaChannelArg) {
  const ChannelArgs args = ChannelArgs().Set(GRPC_ARG_DISABLE_PARSING, 1);
  const char* test_json = "{\"global_param\":5}";
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  EXPECT_EQ((*service_config)->GetGlobalParsedConfig(0), nullptr);
}

TEST_F(ServiceConfigTest, Parser1ErrorInvalidType) {
  const char* test_json = "{\"global_param\":\"5\"}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            absl::StrCat("Service config parsing errors: [",
                         TestParser1::InvalidTypeErrorMessage(), "]"));
}

TEST_F(ServiceConfigTest, Parser1ErrorInvalidValue) {
  const char* test_json = "{\"global_param\":-5}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            absl::StrCat("Service config parsing errors: [",
                         TestParser1::InvalidValueErrorMessage(), "]"));
}

TEST_F(ServiceConfigTest, Parser2BasicTest) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":5}]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  auto parsed_config = ((*vector_ptr)[1]).get();
  EXPECT_EQ(static_cast<TestParsedConfig1*>(parsed_config)->value(), 5);
}

TEST_F(ServiceConfigTest, Parser2DisabledViaChannelArg) {
  const ChannelArgs args = ChannelArgs().Set(GRPC_ARG_DISABLE_PARSING, 1);
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":5}]}";
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  auto parsed_config = ((*vector_ptr)[1]).get();
  EXPECT_EQ(parsed_config, nullptr);
}

TEST_F(ServiceConfigTest, Parser2ErrorInvalidType) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":\"5\"}]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            absl::StrCat("Service config parsing errors: ["
                         "errors parsing methodConfig: ["
                         "index 0: [",
                         TestParser2::InvalidTypeErrorMessage(), "]]]"));
}

TEST_F(ServiceConfigTest, Parser2ErrorInvalidValue) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":-5}]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            absl::StrCat("Service config parsing errors: ["
                         "errors parsing methodConfig: ["
                         "index 0: [",
                         TestParser2::InvalidValueErrorMessage(), "]]]"));
}

TEST(ServiceConfigParserTest, DoubleRegistration) {
  CoreConfiguration::Reset();
  ASSERT_DEATH_IF_SUPPORTED(
      CoreConfiguration::WithSubstituteBuilder builder(
          [](CoreConfiguration::Builder* builder) {
            builder->service_config_parser()->RegisterParser(
                absl::make_unique<ErrorParser>("xyzabc"));
            builder->service_config_parser()->RegisterParser(
                absl::make_unique<ErrorParser>("xyzabc"));
          }),
      "xyzabc.*already registered");
}

// Test parsing with ErrorParsers which always add errors
class ErroredParsersScopingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    builder_ = std::make_unique<CoreConfiguration::WithSubstituteBuilder>(
        [](CoreConfiguration::Builder* builder) {
          builder->service_config_parser()->RegisterParser(
              absl::make_unique<ErrorParser>("ep1"));
          builder->service_config_parser()->RegisterParser(
              absl::make_unique<ErrorParser>("ep2"));
        });
    EXPECT_EQ(
        CoreConfiguration::Get().service_config_parser().GetParserIndex("ep1"),
        0);
    EXPECT_EQ(
        CoreConfiguration::Get().service_config_parser().GetParserIndex("ep2"),
        1);
  }

 private:
  std::unique_ptr<CoreConfiguration::WithSubstituteBuilder> builder_;
};

TEST_F(ErroredParsersScopingTest, GlobalParams) {
  const char* test_json = "{}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            absl::StrCat("Service config parsing errors: [",
                         ErrorParser::GlobalError(), "; ",
                         ErrorParser::GlobalError(), "]"));
}

TEST_F(ErroredParsersScopingTest, MethodParams) {
  const char* test_json = "{\"methodConfig\": [{}]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      service_config.status().message(),
      absl::StrCat("Service config parsing errors: [",
                   ErrorParser::GlobalError(), "; ", ErrorParser::GlobalError(),
                   "; "
                   "errors parsing methodConfig: ["
                   "index 0: [",
                   ErrorParser::MethodError(), "; ", ErrorParser::MethodError(),
                   "]]]"));
}

//
// client_channel parser tests
//

class ClientChannelParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EXPECT_EQ(CoreConfiguration::Get().service_config_parser().GetParserIndex(
                  "client_channel"),
              0);
  }
};

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigPickFirst) {
  const char* test_json = "{\"loadBalancingConfig\": [{\"pick_first\":{}}]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* parsed_config =
      static_cast<internal::ClientChannelGlobalParsedConfig*>(
          (*service_config)->GetGlobalParsedConfig(0));
  auto lb_config = parsed_config->parsed_lb_config();
  EXPECT_EQ(lb_config->name(), "pick_first");
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigRoundRobin) {
  const char* test_json =
      "{\"loadBalancingConfig\": [{\"round_robin\":{}}, {}]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  auto parsed_config = static_cast<internal::ClientChannelGlobalParsedConfig*>(
      (*service_config)->GetGlobalParsedConfig(0));
  auto lb_config = parsed_config->parsed_lb_config();
  EXPECT_EQ(lb_config->name(), "round_robin");
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigGrpclb) {
  const char* test_json =
      "{\"loadBalancingConfig\": "
      "[{\"grpclb\":{\"childPolicy\":[{\"pick_first\":{}}]}}]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* parsed_config =
      static_cast<internal::ClientChannelGlobalParsedConfig*>(
          (*service_config)->GetGlobalParsedConfig(0));
  auto lb_config = parsed_config->parsed_lb_config();
  EXPECT_EQ(lb_config->name(), "grpclb");
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigXds) {
  const char* test_json =
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"does_not_exist\":{} },\n"
      "    { \"xds_cluster_resolver_experimental\":{\n"
      "      \"discoveryMechanisms\": [\n"
      "      { \"clusterName\": \"foo\",\n"
      "        \"type\": \"EDS\"\n"
      "    } ]\n"
      "    } }\n"
      "  ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* parsed_config =
      static_cast<internal::ClientChannelGlobalParsedConfig*>(
          (*service_config)->GetGlobalParsedConfig(0));
  auto lb_config = parsed_config->parsed_lb_config();
  EXPECT_EQ(lb_config->name(), "xds_cluster_resolver_experimental");
}

TEST_F(ClientChannelParserTest, UnknownLoadBalancingConfig) {
  const char* test_json = "{\"loadBalancingConfig\": [{\"unknown\":{}}]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::MatchesRegex(
          "Service config parsing errors: \\["
          "error parsing client channel global parameters:" CHILD_ERROR_TAG
          "field:loadBalancingConfig "
          "error:No known policies in list: unknown.*"));
}

TEST_F(ClientChannelParserTest, InvalidGrpclbLoadBalancingConfig) {
  const char* test_json =
      "{\"loadBalancingConfig\": ["
      "  {\"grpclb\":{\"childPolicy\":1}},"
      "  {\"round_robin\":{}}"
      "]}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::MatchesRegex(
          "Service config parsing errors: \\["
          "error parsing client channel global parameters:" CHILD_ERROR_TAG
          "field:loadBalancingConfig error:"
          "errors validating grpclb LB policy config: \\["
          "field:childPolicy error:type should be array\\].*"));
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingPolicy) {
  const char* test_json = "{\"loadBalancingPolicy\":\"pick_first\"}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* parsed_config =
      static_cast<internal::ClientChannelGlobalParsedConfig*>(
          (*service_config)->GetGlobalParsedConfig(0));
  EXPECT_EQ(parsed_config->parsed_deprecated_lb_policy(), "pick_first");
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingPolicyAllCaps) {
  const char* test_json = "{\"loadBalancingPolicy\":\"PICK_FIRST\"}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* parsed_config =
      static_cast<internal::ClientChannelGlobalParsedConfig*>(
          (*service_config)->GetGlobalParsedConfig(0));
  EXPECT_EQ(parsed_config->parsed_deprecated_lb_policy(), "pick_first");
}

TEST_F(ClientChannelParserTest, UnknownLoadBalancingPolicy) {
  const char* test_json = "{\"loadBalancingPolicy\":\"unknown\"}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::MatchesRegex(
          "Service config parsing errors: \\["
          "error parsing client channel global parameters:" CHILD_ERROR_TAG
          "field:loadBalancingPolicy error:Unknown lb policy.*"));
}

TEST_F(ClientChannelParserTest, LoadBalancingPolicyXdsNotAllowed) {
  const char* test_json =
      "{\"loadBalancingPolicy\":\"xds_cluster_resolver_experimental\"}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::MatchesRegex(
          "Service config parsing errors: \\["
          "error parsing client channel global parameters:" CHILD_ERROR_TAG
          "field:loadBalancingPolicy "
          "error:xds_cluster_resolver_experimental requires "
          "a config. Please use loadBalancingConfig instead.*"));
}

TEST_F(ClientChannelParserTest, ValidTimeout) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"timeout\": \"5s\"\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  auto parsed_config = ((*vector_ptr)[0]).get();
  EXPECT_EQ(
      (static_cast<internal::ClientChannelMethodParsedConfig*>(parsed_config))
          ->timeout(),
      Duration::Seconds(5));
}

TEST_F(ClientChannelParserTest, InvalidTimeout) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"timeout\": \"5sec\"\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::MatchesRegex(
          "Service config parsing errors: \\["
          "errors parsing methodConfig: \\["
          "index 0: \\["
          "error parsing client channel method parameters: " CHILD_ERROR_TAG
          "field:timeout error:type should be STRING of the form given "
          "by google.proto.Duration.*"));
}

TEST_F(ClientChannelParserTest, ValidWaitForReady) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"waitForReady\": true\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  auto parsed_config = ((*vector_ptr)[0]).get();
  ASSERT_TRUE(
      (static_cast<internal::ClientChannelMethodParsedConfig*>(parsed_config))
          ->wait_for_ready()
          .has_value());
  EXPECT_TRUE(
      (static_cast<internal::ClientChannelMethodParsedConfig*>(parsed_config))
          ->wait_for_ready()
          .value());
}

TEST_F(ClientChannelParserTest, InvalidWaitForReady) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"waitForReady\": \"true\"\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::MatchesRegex(
          "Service config parsing errors: \\["
          "errors parsing methodConfig: \\["
          "index 0: \\["
          "error parsing client channel method parameters: " CHILD_ERROR_TAG
          "field:waitForReady error:Type should be true/false.*"));
}

TEST_F(ClientChannelParserTest, ValidHealthCheck) {
  const char* test_json =
      "{\n"
      "  \"healthCheckConfig\": {\n"
      "    \"serviceName\": \"health_check_service_name\"\n"
      "    }\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* parsed_config =
      static_cast<internal::ClientChannelGlobalParsedConfig*>(
          (*service_config)->GetGlobalParsedConfig(0));
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->health_check_service_name(),
            "health_check_service_name");
}

TEST_F(ClientChannelParserTest, InvalidHealthCheckMultipleEntries) {
  const char* test_json =
      "{\n"
      "  \"healthCheckConfig\": {\n"
      "    \"serviceName\": \"health_check_service_name\"\n"
      "    },\n"
      "  \"healthCheckConfig\": {\n"
      "    \"serviceName\": \"health_check_service_name1\"\n"
      "    }\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(service_config.status().message(),
            "JSON parsing failed: ["
            "duplicate key \"healthCheckConfig\" at index 104]");
}

//
// retry parser tests
//

class RetryParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    builder_ = std::make_unique<CoreConfiguration::WithSubstituteBuilder>(
        [](CoreConfiguration::Builder* builder) {
          builder->service_config_parser()->RegisterParser(
              absl::make_unique<internal::RetryServiceConfigParser>());
        });
    EXPECT_EQ(CoreConfiguration::Get().service_config_parser().GetParserIndex(
                  "retry"),
              0);
  }

 private:
  std::unique_ptr<CoreConfiguration::WithSubstituteBuilder> builder_;
};

TEST_F(RetryParserTest, ValidRetryThrottling) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "    \"maxTokens\": 2,\n"
      "    \"tokenRatio\": 1.0\n"
      "  }\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* parsed_config = static_cast<internal::RetryGlobalConfig*>(
      (*service_config)->GetGlobalParsedConfig(0));
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->max_milli_tokens(), 2000);
  EXPECT_EQ(parsed_config->milli_token_ratio(), 1000);
}

TEST_F(RetryParserTest, RetryThrottlingMissingFields) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "  }\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "error parsing retry global parameters:"
                  ".*retryThrottling" CHILD_ERROR_TAG
                  "field:retryThrottling field:maxTokens error:Not found"
                  ".*field:retryThrottling field:tokenRatio error:Not found"));
}

TEST_F(RetryParserTest, InvalidRetryThrottlingNegativeMaxTokens) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "    \"maxTokens\": -2,\n"
      "    \"tokenRatio\": 1.0\n"
      "  }\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "error parsing retry global parameters:"
                  ".*retryThrottling" CHILD_ERROR_TAG
                  "field:retryThrottling field:maxTokens error:should "
                  "be greater than zero"));
}

TEST_F(RetryParserTest, InvalidRetryThrottlingInvalidTokenRatio) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "    \"maxTokens\": 2,\n"
      "    \"tokenRatio\": -1\n"
      "  }\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex("Service config parsing errors: \\["
                                       "error parsing retry global parameters:"
                                       ".*retryThrottling" CHILD_ERROR_TAG
                                       "field:retryThrottling field:tokenRatio "
                                       "error:Failed parsing"));
}

TEST_F(RetryParserTest, ValidRetryPolicy) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 3,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  const auto* parsed_config =
      static_cast<internal::RetryMethodConfig*>(((*vector_ptr)[0]).get());
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->max_attempts(), 3);
  EXPECT_EQ(parsed_config->initial_backoff(), Duration::Seconds(1));
  EXPECT_EQ(parsed_config->max_backoff(), Duration::Minutes(2));
  EXPECT_EQ(parsed_config->backoff_multiplier(), 1.6f);
  EXPECT_EQ(parsed_config->per_attempt_recv_timeout(), absl::nullopt);
  EXPECT_TRUE(
      parsed_config->retryable_status_codes().Contains(GRPC_STATUS_ABORTED));
}

TEST_F(RetryParserTest, InvalidRetryPolicyWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": 5\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "field:retryPolicy error:should be of type object"));
}

TEST_F(RetryParserTest, InvalidRetryPolicyRequiredFieldsMissing) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  ".*field:maxAttempts error:required field missing"
                  ".*field:initialBackoff error:does not exist"
                  ".*field:maxBackoff error:does not exist"
                  ".*field:backoffMultiplier error:required field missing"));
}

TEST_F(RetryParserTest, InvalidRetryPolicyMaxAttemptsWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": \"FOO\",\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  "field:maxAttempts error:should be of type number"));
}

TEST_F(RetryParserTest, InvalidRetryPolicyMaxAttemptsBadValue) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 1,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(service_config.status().message()),
      ::testing::ContainsRegex("Service config parsing errors: \\["
                               "errors parsing methodConfig: \\["
                               "index 0: \\["
                               "error parsing retry method parameters:.*"
                               "retryPolicy" CHILD_ERROR_TAG
                               "field:maxAttempts error:should be at least 2"));
}

TEST_F(RetryParserTest, InvalidRetryPolicyInitialBackoffWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1sec\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  "field:initialBackoff error:type should be STRING of the "
                  "form given by google.proto.Duration"));
}

TEST_F(RetryParserTest, InvalidRetryPolicyInitialBackoffBadValue) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"0s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  "field:initialBackoff error:must be greater than 0"));
}

TEST_F(RetryParserTest, InvalidRetryPolicyMaxBackoffWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120sec\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  "field:maxBackoff error:type should be STRING of the form "
                  "given by google.proto.Duration"));
}

TEST_F(RetryParserTest, InvalidRetryPolicyMaxBackoffBadValue) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"0s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  "field:maxBackoff error:must be greater than 0"));
}

TEST_F(RetryParserTest, InvalidRetryPolicyBackoffMultiplierWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  "field:backoffMultiplier error:should be of type number"));
}

TEST_F(RetryParserTest, InvalidRetryPolicyBackoffMultiplierBadValue) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 0,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  "field:backoffMultiplier error:must be greater than 0"));
}

TEST_F(RetryParserTest, InvalidRetryPolicyEmptyRetryableStatusCodes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"retryableStatusCodes\": []\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  "field:retryableStatusCodes error:must be non-empty"));
}

TEST_F(RetryParserTest, InvalidRetryPolicyRetryableStatusCodesWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"retryableStatusCodes\": 0\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  "field:retryableStatusCodes error:must be of type array"));
}

TEST_F(RetryParserTest, InvalidRetryPolicyUnparseableRetryableStatusCodes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"retryableStatusCodes\": [\"FOO\", 2]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG "field:retryableStatusCodes "
                  "error:failed to parse status code"
                  ".*field:retryableStatusCodes "
                  "error:status codes should be of type string"));
}

TEST_F(RetryParserTest, ValidRetryPolicyWithPerAttemptRecvTimeout) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"perAttemptRecvTimeout\": \"1s\",\n"
      "      \"retryableStatusCodes\": [\"ABORTED\"]\n"
      "    }\n"
      "  } ]\n"
      "}";
  const ChannelArgs args =
      ChannelArgs().Set(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  const auto* parsed_config =
      static_cast<internal::RetryMethodConfig*>(((*vector_ptr)[0]).get());
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->max_attempts(), 2);
  EXPECT_EQ(parsed_config->initial_backoff(), Duration::Seconds(1));
  EXPECT_EQ(parsed_config->max_backoff(), Duration::Minutes(2));
  EXPECT_EQ(parsed_config->backoff_multiplier(), 1.6f);
  EXPECT_EQ(parsed_config->per_attempt_recv_timeout(), Duration::Seconds(1));
  EXPECT_TRUE(
      parsed_config->retryable_status_codes().Contains(GRPC_STATUS_ABORTED));
}

TEST_F(RetryParserTest,
       ValidRetryPolicyWithPerAttemptRecvTimeoutIgnoredWhenHedgingDisabled) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"perAttemptRecvTimeout\": \"1s\",\n"
      "      \"retryableStatusCodes\": [\"ABORTED\"]\n"
      "    }\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  const auto* parsed_config =
      static_cast<internal::RetryMethodConfig*>(((*vector_ptr)[0]).get());
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->max_attempts(), 2);
  EXPECT_EQ(parsed_config->initial_backoff(), Duration::Seconds(1));
  EXPECT_EQ(parsed_config->max_backoff(), Duration::Minutes(2));
  EXPECT_EQ(parsed_config->backoff_multiplier(), 1.6f);
  EXPECT_EQ(parsed_config->per_attempt_recv_timeout(), absl::nullopt);
  EXPECT_TRUE(
      parsed_config->retryable_status_codes().Contains(GRPC_STATUS_ABORTED));
}

TEST_F(RetryParserTest,
       ValidRetryPolicyWithPerAttemptRecvTimeoutAndUnsetRetryableStatusCodes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"perAttemptRecvTimeout\": \"1s\"\n"
      "    }\n"
      "  } ]\n"
      "}";
  const ChannelArgs args =
      ChannelArgs().Set(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  const auto* parsed_config =
      static_cast<internal::RetryMethodConfig*>(((*vector_ptr)[0]).get());
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->max_attempts(), 2);
  EXPECT_EQ(parsed_config->initial_backoff(), Duration::Seconds(1));
  EXPECT_EQ(parsed_config->max_backoff(), Duration::Minutes(2));
  EXPECT_EQ(parsed_config->backoff_multiplier(), 1.6f);
  EXPECT_EQ(parsed_config->per_attempt_recv_timeout(), Duration::Seconds(1));
  EXPECT_TRUE(parsed_config->retryable_status_codes().Empty());
}

TEST_F(RetryParserTest, InvalidRetryPolicyPerAttemptRecvTimeoutUnparseable) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"perAttemptRecvTimeout\": \"1sec\",\n"
      "      \"retryableStatusCodes\": [\"ABORTED\"]\n"
      "    }\n"
      "  } ]\n"
      "}";
  const ChannelArgs args =
      ChannelArgs().Set(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  "field:perAttemptRecvTimeout error:type must be STRING "
                  "of the form given by google.proto.Duration."));
}

TEST_F(RetryParserTest, InvalidRetryPolicyPerAttemptRecvTimeoutWrongType) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"perAttemptRecvTimeout\": 1,\n"
      "      \"retryableStatusCodes\": [\"ABORTED\"]\n"
      "    }\n"
      "  } ]\n"
      "}";
  const ChannelArgs args =
      ChannelArgs().Set(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  "field:perAttemptRecvTimeout error:type must be STRING "
                  "of the form given by google.proto.Duration."));
}

TEST_F(RetryParserTest, InvalidRetryPolicyPerAttemptRecvTimeoutBadValue) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"perAttemptRecvTimeout\": \"0s\",\n"
      "      \"retryableStatusCodes\": [\"ABORTED\"]\n"
      "    }\n"
      "  } ]\n"
      "}";
  const ChannelArgs args =
      ChannelArgs().Set(GRPC_ARG_EXPERIMENTAL_ENABLE_HEDGING, 1);
  auto service_config = ServiceConfigImpl::Create(args, test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing retry method parameters:.*"
                  "retryPolicy" CHILD_ERROR_TAG
                  "field:perAttemptRecvTimeout error:must be greater than 0"));
}

//
// message_size parser tests
//

class MessageSizeParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    builder_ = std::make_unique<CoreConfiguration::WithSubstituteBuilder>(
        [](CoreConfiguration::Builder* builder) {
          builder->service_config_parser()->RegisterParser(
              absl::make_unique<MessageSizeParser>());
        });
    EXPECT_EQ(CoreConfiguration::Get().service_config_parser().GetParserIndex(
                  "message_size"),
              0);
  }

 private:
  std::unique_ptr<CoreConfiguration::WithSubstituteBuilder> builder_;
};

TEST_F(MessageSizeParserTest, Valid) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"maxRequestMessageBytes\": 1024,\n"
      "    \"maxResponseMessageBytes\": 1024\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  const auto* vector_ptr =
      (*service_config)
          ->GetMethodParsedConfigVector(
              grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  auto parsed_config =
      static_cast<MessageSizeParsedConfig*>(((*vector_ptr)[0]).get());
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->limits().max_send_size, 1024);
  EXPECT_EQ(parsed_config->limits().max_recv_size, 1024);
}

TEST_F(MessageSizeParserTest, InvalidMaxRequestMessageBytes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"maxRequestMessageBytes\": -1024\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing message size method parameters:.*"
                  "Message size parser" CHILD_ERROR_TAG
                  "field:maxRequestMessageBytes error:should be non-negative"));
}

TEST_F(MessageSizeParserTest, InvalidMaxResponseMessageBytes) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"maxResponseMessageBytes\": {}\n"
      "  } ]\n"
      "}";
  auto service_config = ServiceConfigImpl::Create(ChannelArgs(), test_json);
  EXPECT_EQ(service_config.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(service_config.status().message()),
              ::testing::ContainsRegex(
                  "Service config parsing errors: \\["
                  "errors parsing methodConfig: \\["
                  "index 0: \\["
                  "error parsing message size method parameters:.*"
                  "Message size parser" CHILD_ERROR_TAG
                  "field:maxResponseMessageBytes error:should be of type "
                  "number"));
}

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
