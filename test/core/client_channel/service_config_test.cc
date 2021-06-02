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

#include "absl/strings/str_cat.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/ext/filters/client_channel/retry_service_config.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/service_config_parser.h"
#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/gpr/string.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

//
// ServiceConfig tests
//

// Set this channel arg to true to disable parsing.
#define GRPC_ARG_DISABLE_PARSING "disable_parsing"

class TestParsedConfig1 : public ServiceConfigParser::ParsedConfig {
 public:
  explicit TestParsedConfig1(int value) : value_(value) {}

  int value() const { return value_; }

 private:
  int value_;
};

class TestParser1 : public ServiceConfigParser::Parser {
 public:
  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParseGlobalParams(
      const grpc_channel_args* args, const Json& json,
      grpc_error_handle* error) override {
    GPR_DEBUG_ASSERT(error != nullptr);
    if (grpc_channel_args_find_bool(args, GRPC_ARG_DISABLE_PARSING, false)) {
      return nullptr;
    }
    auto it = json.object_value().find("global_param");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::NUMBER) {
        *error =
            GRPC_ERROR_CREATE_FROM_STATIC_STRING(InvalidTypeErrorMessage());
        return nullptr;
      }
      int value = gpr_parse_nonnegative_int(it->second.string_value().c_str());
      if (value == -1) {
        *error =
            GRPC_ERROR_CREATE_FROM_STATIC_STRING(InvalidValueErrorMessage());
        return nullptr;
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
  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParsePerMethodParams(
      const grpc_channel_args* args, const Json& json,
      grpc_error_handle* error) override {
    GPR_DEBUG_ASSERT(error != nullptr);
    if (grpc_channel_args_find_bool(args, GRPC_ARG_DISABLE_PARSING, false)) {
      return nullptr;
    }
    auto it = json.object_value().find("method_param");
    if (it != json.object_value().end()) {
      if (it->second.type() != Json::Type::NUMBER) {
        *error =
            GRPC_ERROR_CREATE_FROM_STATIC_STRING(InvalidTypeErrorMessage());
        return nullptr;
      }
      int value = gpr_parse_nonnegative_int(it->second.string_value().c_str());
      if (value == -1) {
        *error =
            GRPC_ERROR_CREATE_FROM_STATIC_STRING(InvalidValueErrorMessage());
        return nullptr;
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
  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParsePerMethodParams(
      const grpc_channel_args* /*arg*/, const Json& /*json*/,
      grpc_error_handle* error) override {
    GPR_DEBUG_ASSERT(error != nullptr);
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(MethodError());
    return nullptr;
  }

  std::unique_ptr<ServiceConfigParser::ParsedConfig> ParseGlobalParams(
      const grpc_channel_args* /*arg*/, const Json& /*json*/,
      grpc_error_handle* error) override {
    GPR_DEBUG_ASSERT(error != nullptr);
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(GlobalError());
    return nullptr;
  }

  static const char* MethodError() { return "ErrorParser : methodError"; }

  static const char* GlobalError() { return "ErrorParser : globalError"; }
};

class ServiceConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfigParser::Shutdown();
    ServiceConfigParser::Init();
    EXPECT_EQ(
        ServiceConfigParser::RegisterParser(absl::make_unique<TestParser1>()),
        0);
    EXPECT_EQ(
        ServiceConfigParser::RegisterParser(absl::make_unique<TestParser2>()),
        1);
  }
};

TEST_F(ServiceConfigTest, ErrorCheck1) {
  const char* test_json = "";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex("JSON parse error"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ServiceConfigTest, BasicTest1) {
  const char* test_json = "{}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
}

TEST_F(ServiceConfigTest, SkipMethodConfigWithNoNameOrEmptyName) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"method_param\":1},"
      "  {\"name\":[], \"method_param\":1},"
      "  {\"name\":[{\"service\":\"TestServ\"}], \"method_param\":2}"
      "]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr = svc_cfg->GetMethodParsedConfigVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  auto parsed_config = ((*vector_ptr)[1]).get();
  EXPECT_EQ(static_cast<TestParsedConfig1*>(parsed_config)->value(), 2);
}

TEST_F(ServiceConfigTest, ErrorDuplicateMethodConfigNames) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"name\":[{\"service\":\"TestServ\"}]},"
      "  {\"name\":[{\"service\":\"TestServ\"}]}"
      "]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("Service config parsing error.*referenced_errors"
                               ".*Method Params.*referenced_errors"
                               ".*methodConfig.*referenced_errors"
                               ".*multiple method configs with same name"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ServiceConfigTest, ErrorDuplicateMethodConfigNamesWithNullMethod) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"name\":[{\"service\":\"TestServ\",\"method\":null}]},"
      "  {\"name\":[{\"service\":\"TestServ\"}]}"
      "]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("Service config parsing error.*referenced_errors"
                               ".*Method Params.*referenced_errors"
                               ".*methodConfig.*referenced_errors"
                               ".*multiple method configs with same name"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ServiceConfigTest, ErrorDuplicateMethodConfigNamesWithEmptyMethod) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"name\":[{\"service\":\"TestServ\",\"method\":\"\"}]},"
      "  {\"name\":[{\"service\":\"TestServ\"}]}"
      "]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("Service config parsing error.*referenced_errors"
                               ".*Method Params.*referenced_errors"
                               ".*methodConfig.*referenced_errors"
                               ".*multiple method configs with same name"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ServiceConfigTest, ErrorDuplicateDefaultMethodConfigs) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"name\":[{}]},"
      "  {\"name\":[{}]}"
      "]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("Service config parsing error.*referenced_errors"
                               ".*Method Params.*referenced_errors"
                               ".*methodConfig.*referenced_errors"
                               ".*multiple default method configs"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ServiceConfigTest, ErrorDuplicateDefaultMethodConfigsWithNullService) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"name\":[{\"service\":null}]},"
      "  {\"name\":[{}]}"
      "]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("Service config parsing error.*referenced_errors"
                               ".*Method Params.*referenced_errors"
                               ".*methodConfig.*referenced_errors"
                               ".*multiple default method configs"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ServiceConfigTest, ErrorDuplicateDefaultMethodConfigsWithEmptyService) {
  const char* test_json =
      "{\"methodConfig\": ["
      "  {\"name\":[{\"service\":\"\"}]},"
      "  {\"name\":[{}]}"
      "]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex("Service config parsing error.*referenced_errors"
                               ".*Method Params.*referenced_errors"
                               ".*methodConfig.*referenced_errors"
                               ".*multiple default method configs"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ServiceConfigTest, ValidMethodConfig) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}]}]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
}

TEST_F(ServiceConfigTest, Parser1BasicTest1) {
  const char* test_json = "{\"global_param\":5}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  EXPECT_EQ((static_cast<TestParsedConfig1*>(svc_cfg->GetGlobalParsedConfig(0)))
                ->value(),
            5);
  EXPECT_EQ(svc_cfg->GetMethodParsedConfigVector(
                grpc_slice_from_static_string("/TestServ/TestMethod")),
            nullptr);
}

TEST_F(ServiceConfigTest, Parser1BasicTest2) {
  const char* test_json = "{\"global_param\":1000}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  EXPECT_EQ((static_cast<TestParsedConfig1*>(svc_cfg->GetGlobalParsedConfig(0)))
                ->value(),
            1000);
}

TEST_F(ServiceConfigTest, Parser1DisabledViaChannelArg) {
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_DISABLE_PARSING), 1);
  grpc_channel_args args = {1, &arg};
  const char* test_json = "{\"global_param\":5}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  EXPECT_EQ(svc_cfg->GetGlobalParsedConfig(0), nullptr);
}

TEST_F(ServiceConfigTest, Parser1ErrorInvalidType) {
  const char* test_json = "{\"global_param\":\"5\"}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(absl::StrCat(
                  "Service config parsing error.*referenced_errors.*"
                  "Global Params.*referenced_errors.*",
                  TestParser1::InvalidTypeErrorMessage())));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ServiceConfigTest, Parser1ErrorInvalidValue) {
  const char* test_json = "{\"global_param\":-5}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(absl::StrCat(
                  "Service config parsing error.*referenced_errors.*"
                  "Global Params.*referenced_errors.*",
                  TestParser1::InvalidValueErrorMessage())));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ServiceConfigTest, Parser2BasicTest) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":5}]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr = svc_cfg->GetMethodParsedConfigVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  auto parsed_config = ((*vector_ptr)[1]).get();
  EXPECT_EQ(static_cast<TestParsedConfig1*>(parsed_config)->value(), 5);
}

TEST_F(ServiceConfigTest, Parser2DisabledViaChannelArg) {
  grpc_arg arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_DISABLE_PARSING), 1);
  grpc_channel_args args = {1, &arg};
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":5}]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(&args, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr = svc_cfg->GetMethodParsedConfigVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  auto parsed_config = ((*vector_ptr)[1]).get();
  EXPECT_EQ(parsed_config, nullptr);
}

TEST_F(ServiceConfigTest, Parser2ErrorInvalidType) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":\"5\"}]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(absl::StrCat(
                  "Service config parsing error.*referenced_errors\":\\[.*"
                  "Method Params.*referenced_errors.*methodConfig.*"
                  "referenced_errors.*",
                  TestParser2::InvalidTypeErrorMessage())));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ServiceConfigTest, Parser2ErrorInvalidValue) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":-5}]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(absl::StrCat(
                  "Service config parsing error.*referenced_errors\":\\[.*"
                  "Method Params.*referenced_errors.*methodConfig.*"
                  "referenced_errors.*",
                  TestParser2::InvalidValueErrorMessage())));
  GRPC_ERROR_UNREF(error);
}

// Test parsing with ErrorParsers which always add errors
class ErroredParsersScopingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfigParser::Shutdown();
    ServiceConfigParser::Init();
    EXPECT_EQ(
        ServiceConfigParser::RegisterParser(absl::make_unique<ErrorParser>()),
        0);
    EXPECT_EQ(
        ServiceConfigParser::RegisterParser(absl::make_unique<ErrorParser>()),
        1);
  }
};

TEST_F(ErroredParsersScopingTest, GlobalParams) {
  const char* test_json = "{}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex(absl::StrCat(
          "Service config parsing error.*referenced_errors\":\\[.*"
          "Global Params.*referenced_errors.*",
          ErrorParser::GlobalError(), ".*", ErrorParser::GlobalError())));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ErroredParsersScopingTest, MethodParams) {
  const char* test_json = "{\"methodConfig\": [{}]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(
      grpc_error_std_string(error),
      ::testing::ContainsRegex(absl::StrCat(
          "Service config parsing error.*referenced_errors\":\\[.*"
          "Global Params.*referenced_errors.*",
          ErrorParser::GlobalError(), ".*", ErrorParser::GlobalError(),
          ".*Method Params.*referenced_errors.*methodConfig.*"
          "referenced_errors.*",
          ErrorParser::MethodError(), ".*", ErrorParser::MethodError())));
  GRPC_ERROR_UNREF(error);
}

//
// client_channel parser tests
//

class ClientChannelParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfigParser::Shutdown();
    ServiceConfigParser::Init();
    EXPECT_EQ(
        ServiceConfigParser::RegisterParser(
            absl::make_unique<internal::ClientChannelServiceConfigParser>()),
        0);
  }
};

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigPickFirst) {
  const char* test_json = "{\"loadBalancingConfig\": [{\"pick_first\":{}}]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  auto lb_config = parsed_config->parsed_lb_config();
  EXPECT_STREQ(lb_config->name(), "pick_first");
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigRoundRobin) {
  const char* test_json =
      "{\"loadBalancingConfig\": [{\"round_robin\":{}}, {}]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  auto parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  auto lb_config = parsed_config->parsed_lb_config();
  EXPECT_STREQ(lb_config->name(), "round_robin");
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigGrpclb) {
  const char* test_json =
      "{\"loadBalancingConfig\": "
      "[{\"grpclb\":{\"childPolicy\":[{\"pick_first\":{}}]}}]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  auto lb_config = parsed_config->parsed_lb_config();
  EXPECT_STREQ(lb_config->name(), "grpclb");
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  auto lb_config = parsed_config->parsed_lb_config();
  EXPECT_STREQ(lb_config->name(), "xds_cluster_resolver_experimental");
}

TEST_F(ClientChannelParserTest, UnknownLoadBalancingConfig) {
  const char* test_json = "{\"loadBalancingConfig\": [{\"unknown\":{}}]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Global Params.*referenced_errors.*"
                  "Client channel global parser.*referenced_errors.*"
                  "field:loadBalancingConfig.*referenced_errors.*"
                  "No known policies in list: unknown"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ClientChannelParserTest, InvalidGrpclbLoadBalancingConfig) {
  const char* test_json =
      "{\"loadBalancingConfig\": ["
      "  {\"grpclb\":{\"childPolicy\":1}},"
      "  {\"round_robin\":{}}"
      "]}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Global Params.*referenced_errors.*"
                  "Client channel global parser.*referenced_errors.*"
                  "field:loadBalancingConfig.*referenced_errors.*"
                  "GrpcLb Parser.*referenced_errors.*"
                  "field:childPolicy.*referenced_errors.*"
                  "type should be array"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingPolicy) {
  const char* test_json = "{\"loadBalancingPolicy\":\"pick_first\"}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  EXPECT_EQ(parsed_config->parsed_deprecated_lb_policy(), "pick_first");
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingPolicyAllCaps) {
  const char* test_json = "{\"loadBalancingPolicy\":\"PICK_FIRST\"}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  EXPECT_EQ(parsed_config->parsed_deprecated_lb_policy(), "pick_first");
}

TEST_F(ClientChannelParserTest, UnknownLoadBalancingPolicy) {
  const char* test_json = "{\"loadBalancingPolicy\":\"unknown\"}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Global Params.*referenced_errors.*"
                  "Client channel global parser.*referenced_errors.*"
                  "field:loadBalancingPolicy error:Unknown lb policy"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ClientChannelParserTest, LoadBalancingPolicyXdsNotAllowed) {
  const char* test_json =
      "{\"loadBalancingPolicy\":\"xds_cluster_resolver_experimental\"}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Global Params.*referenced_errors.*"
                  "Client channel global parser.*referenced_errors.*"
                  "field:loadBalancingPolicy "
                  "error:xds_cluster_resolver_experimental requires "
                  "a config. Please use loadBalancingConfig instead."));
  GRPC_ERROR_UNREF(error);
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr = svc_cfg->GetMethodParsedConfigVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  auto parsed_config = ((*vector_ptr)[0]).get();
  EXPECT_EQ((static_cast<grpc_core::internal::ClientChannelMethodParsedConfig*>(
                 parsed_config))
                ->timeout(),
            5000);
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Method Params.*referenced_errors.*"
                  "methodConfig.*referenced_errors.*"
                  "Client channel parser.*referenced_errors.*"
                  "field:timeout error:type should be STRING of the form given "
                  "by google.proto.Duration"));
  GRPC_ERROR_UNREF(error);
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr = svc_cfg->GetMethodParsedConfigVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  auto parsed_config = ((*vector_ptr)[0]).get();
  ASSERT_TRUE(
      (static_cast<grpc_core::internal::ClientChannelMethodParsedConfig*>(
           parsed_config))
          ->wait_for_ready()
          .has_value());
  EXPECT_TRUE(
      (static_cast<grpc_core::internal::ClientChannelMethodParsedConfig*>(
           parsed_config))
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Method Params.*referenced_errors.*"
                  "methodConfig.*referenced_errors.*"
                  "Client channel parser.*referenced_errors.*"
                  "field:waitForReady error:Type should be true/false"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ClientChannelParserTest, ValidHealthCheck) {
  const char* test_json =
      "{\n"
      "  \"healthCheckConfig\": {\n"
      "    \"serviceName\": \"health_check_service_name\"\n"
      "    }\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "JSON parsing failed.*referenced_errors.*"
                  "duplicate key \"healthCheckConfig\" at index 104"));
  GRPC_ERROR_UNREF(error);
}

//
// retry parser tests
//

class RetryParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfigParser::Shutdown();
    ServiceConfigParser::Init();
    EXPECT_EQ(ServiceConfigParser::RegisterParser(
                  absl::make_unique<internal::RetryServiceConfigParser>()),
              0);
  }
};

TEST_F(RetryParserTest, ValidRetryThrottling) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "    \"maxTokens\": 2,\n"
      "    \"tokenRatio\": 1.0\n"
      "  }\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* parsed_config =
      static_cast<grpc_core::internal::RetryGlobalConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Global Params.*referenced_errors.*"
                  "retryThrottling.*referenced_errors.*"
                  "field:retryThrottling field:maxTokens error:Not found.*"
                  "field:retryThrottling field:tokenRatio error:Not found"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(RetryParserTest, InvalidRetryThrottlingNegativeMaxTokens) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "    \"maxTokens\": -2,\n"
      "    \"tokenRatio\": 1.0\n"
      "  }\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Global Params.*referenced_errors.*"
                  "retryThrottling.*referenced_errors.*"
                  "field:retryThrottling field:maxTokens error:should "
                  "be greater than zero"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(RetryParserTest, InvalidRetryThrottlingInvalidTokenRatio) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "    \"maxTokens\": 2,\n"
      "    \"tokenRatio\": -1\n"
      "  }\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Global Params.*referenced_errors.*"
                  "retryThrottling.*referenced_errors.*"
                  "field:retryThrottling field:tokenRatio "
                  "error:Failed parsing"));
  GRPC_ERROR_UNREF(error);
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr = svc_cfg->GetMethodParsedConfigVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  ASSERT_NE(vector_ptr, nullptr);
  const auto* parsed_config =
      static_cast<grpc_core::internal::RetryMethodConfig*>(
          ((*vector_ptr)[0]).get());
  ASSERT_NE(parsed_config, nullptr);
  EXPECT_EQ(parsed_config->max_attempts(), 3);
  EXPECT_EQ(parsed_config->initial_backoff(), 1000);
  EXPECT_EQ(parsed_config->max_backoff(), 120000);
  EXPECT_EQ(parsed_config->backoff_multiplier(), 1.6f);
  EXPECT_TRUE(
      parsed_config->retryable_status_codes().Contains(GRPC_STATUS_ABORTED));
}

TEST_F(RetryParserTest, InvalidRetryPolicyMaxAttempts) {
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Method Params.*referenced_errors.*"
                  "methodConfig.*referenced_errors.*"
                  "retryPolicy.*referenced_errors.*"
                  "field:maxAttempts error:should be at least 2"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(RetryParserTest, InvalidRetryPolicyInitialBackoff) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 1,\n"
      "      \"initialBackoff\": \"1sec\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Method Params.*referenced_errors.*"
                  "methodConfig.*referenced_errors.*"
                  "retryPolicy.*referenced_errors.*"
                  "field:initialBackoff error:type should be STRING of the "
                  "form given by google.proto.Duration"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(RetryParserTest, InvalidRetryPolicyMaxBackoff) {
  const char* test_json =
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"TestServ\", \"method\": \"TestMethod\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 1,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120sec\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Method Params.*referenced_errors.*"
                  "methodConfig.*referenced_errors.*"
                  "retryPolicy.*referenced_errors.*"
                  "field:maxBackoff error:type should be STRING of the form "
                  "given by google.proto.Duration"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(RetryParserTest, InvalidRetryPolicyBackoffMultiplier) {
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
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Method Params.*referenced_errors.*"
                  "methodConfig.*referenced_errors.*"
                  "retryPolicy.*referenced_errors.*"
                  "field:backoffMultiplier error:should be of type number"));
  GRPC_ERROR_UNREF(error);
}

TEST_F(RetryParserTest, InvalidRetryPolicyRetryableStatusCodes) {
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
      "      \"backoffMultiplier\": \"1.6\",\n"
      "      \"retryableStatusCodes\": []\n"
      "    }\n"
      "  } ]\n"
      "}";
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Method Params.*referenced_errors.*"
                  "methodConfig.*referenced_errors.*"
                  "retryPolicy.*referenced_errors.*"
                  "field:retryableStatusCodes error:should be non-empty"));
  GRPC_ERROR_UNREF(error);
}

//
// message_size parser tests
//

class MessageSizeParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfigParser::Shutdown();
    ServiceConfigParser::Init();
    EXPECT_EQ(ServiceConfigParser::RegisterParser(
                  absl::make_unique<MessageSizeParser>()),
              0);
  }
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  const auto* vector_ptr = svc_cfg->GetMethodParsedConfigVector(
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Method Params.*referenced_errors.*"
                  "methodConfig.*referenced_errors.*"
                  "Message size parser.*referenced_errors.*"
                  "field:maxRequestMessageBytes error:should be non-negative"));
  GRPC_ERROR_UNREF(error);
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(nullptr, test_json, &error);
  EXPECT_THAT(grpc_error_std_string(error),
              ::testing::ContainsRegex(
                  "Service config parsing error.*referenced_errors.*"
                  "Method Params.*referenced_errors.*"
                  "methodConfig.*referenced_errors.*"
                  "Message size parser.*referenced_errors.*"
                  "field:maxResponseMessageBytes error:should be of type "
                  "number"));
  GRPC_ERROR_UNREF(error);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
