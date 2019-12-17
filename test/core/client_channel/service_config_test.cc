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

#include <regex>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/gpr/string.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

class TestParsedConfig1 : public ServiceConfig::ParsedConfig {
 public:
  TestParsedConfig1(int value) : value_(value) {}

  int value() const { return value_; }

 private:
  int value_;
};

class TestParser1 : public ServiceConfig::Parser {
 public:
  std::unique_ptr<ServiceConfig::ParsedConfig> ParseGlobalParams(
      const grpc_json* json, grpc_error** error) override {
    GPR_DEBUG_ASSERT(error != nullptr);
    for (grpc_json* field = json->child; field != nullptr;
         field = field->next) {
      if (strcmp(field->key, "global_param") == 0) {
        if (field->type != GRPC_JSON_NUMBER) {
          *error =
              GRPC_ERROR_CREATE_FROM_STATIC_STRING(InvalidTypeErrorMessage());
          return nullptr;
        }
        int value = gpr_parse_nonnegative_int(field->value);
        if (value == -1) {
          *error =
              GRPC_ERROR_CREATE_FROM_STATIC_STRING(InvalidValueErrorMessage());
          return nullptr;
        }
        return MakeUnique<TestParsedConfig1>(value);
      }
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

class TestParser2 : public ServiceConfig::Parser {
 public:
  std::unique_ptr<ServiceConfig::ParsedConfig> ParsePerMethodParams(
      const grpc_json* json, grpc_error** error) override {
    GPR_DEBUG_ASSERT(error != nullptr);
    for (grpc_json* field = json->child; field != nullptr;
         field = field->next) {
      if (field->key == nullptr || strcmp(field->key, "name") == 0) {
        continue;
      }
      if (strcmp(field->key, "method_param") == 0) {
        if (field->type != GRPC_JSON_NUMBER) {
          *error =
              GRPC_ERROR_CREATE_FROM_STATIC_STRING(InvalidTypeErrorMessage());
          return nullptr;
        }
        int value = gpr_parse_nonnegative_int(field->value);
        if (value == -1) {
          *error =
              GRPC_ERROR_CREATE_FROM_STATIC_STRING(InvalidValueErrorMessage());
          return nullptr;
        }
        return MakeUnique<TestParsedConfig1>(value);
      }
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
class ErrorParser : public ServiceConfig::Parser {
 public:
  std::unique_ptr<ServiceConfig::ParsedConfig> ParsePerMethodParams(
      const grpc_json* /*json*/, grpc_error** error) override {
    GPR_DEBUG_ASSERT(error != nullptr);
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(MethodError());
    return nullptr;
  }

  std::unique_ptr<ServiceConfig::ParsedConfig> ParseGlobalParams(
      const grpc_json* /*json*/, grpc_error** error) override {
    GPR_DEBUG_ASSERT(error != nullptr);
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(GlobalError());
    return nullptr;
  }

  static const char* MethodError() { return "ErrorParser : methodError"; }

  static const char* GlobalError() { return "ErrorParser : globalError"; }
};

void VerifyRegexMatch(grpc_error* error, const std::regex& e) {
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
}

class ServiceConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfig::Shutdown();
    ServiceConfig::Init();
    EXPECT_TRUE(ServiceConfig::RegisterParser(MakeUnique<TestParser1>()) == 0);
    EXPECT_TRUE(ServiceConfig::RegisterParser(MakeUnique<TestParser2>()) == 1);
  }
};

TEST_F(ServiceConfigTest, ErrorCheck1) {
  const char* test_json = "";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string("failed to parse JSON for service config"));
  VerifyRegexMatch(error, e);
}

TEST_F(ServiceConfigTest, BasicTest1) {
  const char* test_json = "{}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  EXPECT_TRUE(error == GRPC_ERROR_NONE);
}

TEST_F(ServiceConfigTest, ErrorNoNames) {
  const char* test_json = "{\"methodConfig\": [{\"blah\":1}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Method "
                  "Params)(.*)(referenced_errors)(.*)(No names "
                  "found)(.*)(methodConfig)(.*)(referenced_errors)(.*)(No "
                  "names specified)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ServiceConfigTest, ErrorNoNamesWithMultipleMethodConfigs) {
  const char* test_json =
      "{\"methodConfig\": [{}, {\"name\":[{\"service\":\"TestServ\"}]}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Method "
                  "Params)(.*)(referenced_errors)(.*)(No names "
                  "found)(.*)(methodConfig)(.*)(referenced_errors)(.*)(No "
                  "names specified)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ServiceConfigTest, ValidMethodConfig) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}]}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  EXPECT_TRUE(error == GRPC_ERROR_NONE);
}

TEST_F(ServiceConfigTest, Parser1BasicTest1) {
  const char* test_json = "{\"global_param\":5}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  EXPECT_TRUE(
      (static_cast<TestParsedConfig1*>(svc_cfg->GetGlobalParsedConfig(0)))
          ->value() == 5);
  EXPECT_TRUE(svc_cfg->GetMethodParsedConfigVector(
                  grpc_slice_from_static_string("/TestServ/TestMethod")) ==
              nullptr);
}

TEST_F(ServiceConfigTest, Parser1BasicTest2) {
  const char* test_json = "{\"global_param\":1000}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  EXPECT_TRUE(
      (static_cast<TestParsedConfig1*>(svc_cfg->GetGlobalParsedConfig(0)))
          ->value() == 1000);
}

TEST_F(ServiceConfigTest, Parser1ErrorInvalidType) {
  const char* test_json = "{\"global_param\":\"5\"}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string("(Service config parsing "
                           "error)(.*)(referenced_errors)(.*)(Global "
                           "Params)(.*)(referenced_errors)(.*)") +
               TestParser1::InvalidTypeErrorMessage());
  VerifyRegexMatch(error, e);
}

TEST_F(ServiceConfigTest, Parser1ErrorInvalidValue) {
  const char* test_json = "{\"global_param\":-5}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string("(Service config parsing "
                           "error)(.*)(referenced_errors)(.*)(Global "
                           "Params)(.*)(referenced_errors)(.*)") +
               TestParser1::InvalidValueErrorMessage());
  VerifyRegexMatch(error, e);
}

TEST_F(ServiceConfigTest, Parser2BasicTest) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":5}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* vector_ptr = svc_cfg->GetMethodParsedConfigVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  EXPECT_TRUE(vector_ptr != nullptr);
  auto parsed_config = ((*vector_ptr)[1]).get();
  EXPECT_TRUE(static_cast<TestParsedConfig1*>(parsed_config)->value() == 5);
}

TEST_F(ServiceConfigTest, Parser2ErrorInvalidType) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":\"5\"}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  std::regex e(std::string("(Service config parsing "
                           "error)(.*)(referenced_errors\":\\[)(.*)(Method "
                           "Params)(.*)(referenced_errors)(.*)(methodConfig)("
                           ".*)(referenced_errors)(.*)") +
               TestParser2::InvalidTypeErrorMessage());
  VerifyRegexMatch(error, e);
}

TEST_F(ServiceConfigTest, Parser2ErrorInvalidValue) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":-5}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  std::regex e(std::string("(Service config parsing "
                           "error)(.*)(referenced_errors\":\\[)(.*)(Method "
                           "Params)(.*)(referenced_errors)()(.*)(methodConfig)("
                           ".*)(referenced_errors)(.*)") +
               TestParser2::InvalidValueErrorMessage());
  VerifyRegexMatch(error, e);
}

// Test parsing with ErrorParsers which always add errors
class ErroredParsersScopingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfig::Shutdown();
    ServiceConfig::Init();
    EXPECT_TRUE(ServiceConfig::RegisterParser(MakeUnique<ErrorParser>()) == 0);
    EXPECT_TRUE(ServiceConfig::RegisterParser(MakeUnique<ErrorParser>()) == 1);
  }
};

TEST_F(ErroredParsersScopingTest, GlobalParams) {
  const char* test_json = "{}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  std::regex e(std::string("(Service config parsing "
                           "error)(.*)(referenced_errors\":\\[)(.*)(Global "
                           "Params)(.*)(referenced_errors)()(.*)") +
               ErrorParser::GlobalError() + std::string("(.*)") +
               ErrorParser::GlobalError());
  VerifyRegexMatch(error, e);
}

TEST_F(ErroredParsersScopingTest, MethodParams) {
  const char* test_json = "{\"methodConfig\": [{}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors\":\\[)(.*)(Global "
                  "Params)(.*)(referenced_errors)()(.*)") +
      ErrorParser::GlobalError() + std::string("(.*)") +
      ErrorParser::GlobalError() +
      std::string("(.*)(Method "
                  "Params)(.*)(referenced_errors)(.*)(field:methodConfig "
                  "error:No names "
                  "found)(.*)(methodConfig)(.*)(referenced_errors)(.*)") +
      ErrorParser::MethodError() + std::string("(.*)") +
      ErrorParser::MethodError() + std::string("(.*)(No names specified)"));
  VerifyRegexMatch(error, e);
}

class ClientChannelParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfig::Shutdown();
    ServiceConfig::Init();
    EXPECT_TRUE(ServiceConfig::RegisterParser(
                    MakeUnique<internal::ClientChannelServiceConfigParser>()) ==
                0);
  }
};

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigPickFirst) {
  const char* test_json = "{\"loadBalancingConfig\": [{\"pick_first\":{}}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  auto lb_config = parsed_config->parsed_lb_config();
  EXPECT_TRUE(strcmp(lb_config->name(), "pick_first") == 0);
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigRoundRobin) {
  const char* test_json =
      "{\"loadBalancingConfig\": [{\"round_robin\":{}}, {}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  auto parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  auto lb_config = parsed_config->parsed_lb_config();
  EXPECT_TRUE(strcmp(lb_config->name(), "round_robin") == 0);
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigGrpclb) {
  const char* test_json =
      "{\"loadBalancingConfig\": "
      "[{\"grpclb\":{\"childPolicy\":[{\"pick_first\":{}}]}}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  auto lb_config = parsed_config->parsed_lb_config();
  EXPECT_TRUE(strcmp(lb_config->name(), "grpclb") == 0);
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigXds) {
  const char* test_json =
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"does_not_exist\":{} },\n"
      "    { \"xds_experimental\":{ \"balancerName\": \"fake:///lb\" } }\n"
      "  ]\n"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  auto lb_config = parsed_config->parsed_lb_config();
  EXPECT_TRUE(strcmp(lb_config->name(), "xds_experimental") == 0);
}

TEST_F(ClientChannelParserTest, UnknownLoadBalancingConfig) {
  const char* test_json = "{\"loadBalancingConfig\": [{\"unknown\":{}}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Global "
                  "Params)(.*)(referenced_errors)(.*)(Client channel global "
                  "parser)(.*)(referenced_errors)(.*)(field:"
                  "loadBalancingConfig error:No known policy)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ClientChannelParserTest, InvalidGrpclbLoadBalancingConfig) {
  const char* test_json =
      "{\"loadBalancingConfig\": "
      "[{\"grpclb\":{\"childPolicy\":[{\"unknown\":{}}]}}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Global "
                  "Params)(.*)(referenced_errors)(.*)(Client channel global "
                  "parser)(.*)(referenced_errors)(.*)(GrpcLb "
                  "Parser)(.*)(referenced_errors)(.*)(field:childPolicy "
                  "error:No known policy)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingPolicy) {
  const char* test_json = "{\"loadBalancingPolicy\":\"pick_first\"}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  const auto* lb_policy = parsed_config->parsed_deprecated_lb_policy();
  ASSERT_TRUE(lb_policy != nullptr);
  EXPECT_TRUE(strcmp(lb_policy, "pick_first") == 0);
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingPolicyAllCaps) {
  const char* test_json = "{\"loadBalancingPolicy\":\"PICK_FIRST\"}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  const auto* lb_policy = parsed_config->parsed_deprecated_lb_policy();
  ASSERT_TRUE(lb_policy != nullptr);
  EXPECT_TRUE(strcmp(lb_policy, "pick_first") == 0);
}

TEST_F(ClientChannelParserTest, UnknownLoadBalancingPolicy) {
  const char* test_json = "{\"loadBalancingPolicy\":\"unknown\"}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Global "
                  "Params)(.*)(referenced_errors)(.*)(Client channel global "
                  "parser)(.*)(referenced_errors)(.*)(field:"
                  "loadBalancingPolicy error:Unknown lb policy)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ClientChannelParserTest, LoadBalancingPolicyXdsNotAllowed) {
  const char* test_json = "{\"loadBalancingPolicy\":\"xds_experimental\"}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Global "
                  "Params)(.*)(referenced_errors)(.*)(Client channel global "
                  "parser)(.*)(referenced_errors)(.*)(field:"
                  "loadBalancingPolicy error:xds_experimental requires a "
                  "config. Please use loadBalancingConfig instead.)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ClientChannelParserTest, ValidRetryThrottling) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "    \"maxTokens\": 2,\n"
      "    \"tokenRatio\": 1.0\n"
      "  }\n"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  const auto retryThrottling = parsed_config->retry_throttling();
  ASSERT_TRUE(retryThrottling.has_value());
  EXPECT_EQ(retryThrottling.value().max_milli_tokens, 2000);
  EXPECT_EQ(retryThrottling.value().milli_token_ratio, 1000);
}

TEST_F(ClientChannelParserTest, RetryThrottlingMissingFields) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "  }\n"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Global "
                  "Params)(.*)(referenced_errors)(.*)(Client channel global "
                  "parser)(.*)(referenced_errors)(.*)(field:retryThrottling "
                  "field:maxTokens error:Not found)(.*)(field:retryThrottling "
                  "field:tokenRatio error:Not found)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ClientChannelParserTest, InvalidRetryThrottlingNegativeMaxTokens) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "    \"maxTokens\": -2,\n"
      "    \"tokenRatio\": 1.0\n"
      "  }\n"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Global "
                  "Params)(.*)(referenced_errors)(.*)(Client channel global "
                  "parser)(.*)(referenced_errors)(.*)(field:retryThrottling "
                  "field:maxTokens error:should be greater than zero)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ClientChannelParserTest, InvalidRetryThrottlingInvalidTokenRatio) {
  const char* test_json =
      "{\n"
      "  \"retryThrottling\": {\n"
      "    \"maxTokens\": 2,\n"
      "    \"tokenRatio\": -1\n"
      "  }\n"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Global "
                  "Params)(.*)(referenced_errors)(.*)(Client channel global "
                  "parser)(.*)(referenced_errors)(.*)(field:retryThrottling "
                  "field:tokenRatio error:Failed parsing)"));
  VerifyRegexMatch(error, e);
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* vector_ptr = svc_cfg->GetMethodParsedConfigVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  EXPECT_TRUE(vector_ptr != nullptr);
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Method "
                  "Params)(.*)(referenced_errors)(.*)(methodConfig)(.*)("
                  "referenced_errors)(.*)(Client channel "
                  "parser)(.*)(referenced_errors)(.*)(field:timeout "
                  "error:Failed parsing)"));
  VerifyRegexMatch(error, e);
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* vector_ptr = svc_cfg->GetMethodParsedConfigVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  EXPECT_TRUE(vector_ptr != nullptr);
  auto parsed_config = ((*vector_ptr)[0]).get();
  EXPECT_TRUE(
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Method "
                  "Params)(.*)(referenced_errors)(.*)(methodConfig)(.*)("
                  "referenced_errors)(.*)(Client channel "
                  "parser)(.*)(referenced_errors)(.*)(field:waitForReady "
                  "error:Type should be true/false)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ClientChannelParserTest, ValidRetryPolicy) {
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* vector_ptr = svc_cfg->GetMethodParsedConfigVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  EXPECT_TRUE(vector_ptr != nullptr);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelMethodParsedConfig*>(
          ((*vector_ptr)[0]).get());
  EXPECT_TRUE(parsed_config->retry_policy() != nullptr);
  EXPECT_EQ(parsed_config->retry_policy()->max_attempts, 3);
  EXPECT_EQ(parsed_config->retry_policy()->initial_backoff, 1000);
  EXPECT_EQ(parsed_config->retry_policy()->max_backoff, 120000);
  EXPECT_EQ(parsed_config->retry_policy()->backoff_multiplier, 1.6f);
  EXPECT_TRUE(parsed_config->retry_policy()->retryable_status_codes.Contains(
      GRPC_STATUS_ABORTED));
}

TEST_F(ClientChannelParserTest, InvalidRetryPolicyMaxAttempts) {
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string(
      "(Service config parsing "
      "error)(.*)(referenced_errors)(.*)(Method "
      "Params)(.*)(referenced_errors)(.*)(methodConfig)(.*)(referenced_errors)("
      ".*)(Client channel "
      "parser)(.*)(referenced_errors)(.*)(retryPolicy)(.*)(referenced_errors)(."
      "*)(field:maxAttempts error:should be at least 2)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ClientChannelParserTest, InvalidRetryPolicyInitialBackoff) {
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string(
      "(Service config parsing "
      "error)(.*)(referenced_errors)(.*)(Method "
      "Params)(.*)(referenced_errors)(.*)(methodConfig)(.*)(referenced_errors)("
      ".*)(Client channel "
      "parser)(.*)(referenced_errors)(.*)(retryPolicy)(.*)(referenced_errors)(."
      "*)(field:initialBackoff error:Failed to parse)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ClientChannelParserTest, InvalidRetryPolicyMaxBackoff) {
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string(
      "(Service config parsing "
      "error)(.*)(referenced_errors)(.*)(Method "
      "Params)(.*)(referenced_errors)(.*)(methodConfig)(.*)(referenced_errors)("
      ".*)(Client channel "
      "parser)(.*)(referenced_errors)(.*)(retryPolicy)(.*)(referenced_errors)(."
      "*)(field:maxBackoff error:failed to parse)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ClientChannelParserTest, InvalidRetryPolicyBackoffMultiplier) {
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string(
      "(Service config parsing "
      "error)(.*)(referenced_errors)(.*)(Method "
      "Params)(.*)(referenced_errors)(.*)(methodConfig)(.*)(referenced_errors)("
      ".*)(Client channel "
      "parser)(.*)(referenced_errors)(.*)(retryPolicy)(.*)(referenced_errors)(."
      "*)(field:backoffMultiplier error:should be of type number)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ClientChannelParserTest, InvalidRetryPolicyRetryableStatusCodes) {
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string(
      "(Service config parsing "
      "error)(.*)(referenced_errors)(.*)(Method "
      "Params)(.*)(referenced_errors)(.*)(methodConfig)(.*)(referenced_errors)("
      ".*)(Client channel "
      "parser)(.*)(referenced_errors)(.*)(retryPolicy)(.*)(referenced_errors)(."
      "*)(field:retryableStatusCodes error:should be non-empty)"));
  VerifyRegexMatch(error, e);
}

TEST_F(ClientChannelParserTest, ValidHealthCheck) {
  const char* test_json =
      "{\n"
      "  \"healthCheckConfig\": {\n"
      "    \"serviceName\": \"health_check_service_name\"\n"
      "    }\n"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_config =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedConfig*>(
          svc_cfg->GetGlobalParsedConfig(0));
  ASSERT_TRUE(parsed_config != nullptr);
  EXPECT_EQ(strcmp(parsed_config->health_check_service_name(),
                   "health_check_service_name"),
            0);
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Global "
                  "Params)(.*)(referenced_errors)(.*)(field:healthCheckConfig "
                  "error:Duplicate entry)"));
  VerifyRegexMatch(error, e);
}

class MessageSizeParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfig::Shutdown();
    ServiceConfig::Init();
    EXPECT_TRUE(
        ServiceConfig::RegisterParser(MakeUnique<MessageSizeParser>()) == 0);
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* vector_ptr = svc_cfg->GetMethodParsedConfigVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  EXPECT_TRUE(vector_ptr != nullptr);
  auto parsed_config =
      static_cast<MessageSizeParsedConfig*>(((*vector_ptr)[0]).get());
  ASSERT_TRUE(parsed_config != nullptr);
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Method "
                  "Params)(.*)(referenced_errors)(.*)(methodConfig)(.*)("
                  "referenced_errors)(.*)(Message size "
                  "parser)(.*)(referenced_errors)(.*)(field:"
                  "maxRequestMessageBytes error:should be non-negative)"));
  VerifyRegexMatch(error, e);
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Method "
                  "Params)(.*)(referenced_errors)(.*)(methodConfig)(.*)("
                  "referenced_errors)(.*)(Message size "
                  "parser)(.*)(referenced_errors)(.*)(field:"
                  "maxResponseMessageBytes error:should be of type number)"));
  VerifyRegexMatch(error, e);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
// Regexes don't work in old libstdc++ versions, so just skip testing in those
// cases
#if defined(__GLIBCXX__) && (__GLIBCXX__ <= 20150623)
  gpr_log(GPR_ERROR,
          "Skipping service_config_test since std::regex is not supported on "
          "this system.");
  return 0;
#endif
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
