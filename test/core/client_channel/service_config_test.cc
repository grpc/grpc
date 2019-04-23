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
#include "src/core/ext/filters/client_channel/health/health_check_parser.h"
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/message_size/message_size_parser.h"
#include "src/core/lib/gpr/string.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

class TestParsedObject1 : public ServiceConfigParsedObject {
 public:
  TestParsedObject1(int value) : value_(value) {}

  int value() const { return value_; }

 private:
  int value_;
};

class TestParser1 : public ServiceConfigParser {
 public:
  UniquePtr<ServiceConfigParsedObject> ParseGlobalParams(
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
        return UniquePtr<ServiceConfigParsedObject>(
            New<TestParsedObject1>(value));
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

class TestParser2 : public ServiceConfigParser {
 public:
  UniquePtr<ServiceConfigParsedObject> ParsePerMethodParams(
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
        return UniquePtr<ServiceConfigParsedObject>(
            New<TestParsedObject1>(value));
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
class ErrorParser : public ServiceConfigParser {
 public:
  UniquePtr<ServiceConfigParsedObject> ParsePerMethodParams(
      const grpc_json* json, grpc_error** error) override {
    GPR_DEBUG_ASSERT(error != nullptr);
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(MethodError());
    return nullptr;
  }

  UniquePtr<ServiceConfigParsedObject> ParseGlobalParams(
      const grpc_json* json, grpc_error** error) override {
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
    ServiceConfig::Shutdown();
    ServiceConfig::Init();
    EXPECT_TRUE(ServiceConfig::RegisterParser(
                    UniquePtr<ServiceConfigParser>(New<TestParser1>())) == 0);
    EXPECT_TRUE(ServiceConfig::RegisterParser(
                    UniquePtr<ServiceConfigParser>(New<TestParser2>())) == 1);
  }
};

TEST_F(ServiceConfigTest, ErrorCheck1) {
  const char* test_json = "";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string("failed to parse JSON for service config"));
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
  EXPECT_TRUE((static_cast<TestParsedObject1*>(
                   svc_cfg->GetParsedGlobalServiceConfigObject(0)))
                  ->value() == 5);
  EXPECT_TRUE(svc_cfg->GetMethodServiceConfigObjectsVector(
                  grpc_slice_from_static_string("/TestServ/TestMethod")) ==
              nullptr);
}

TEST_F(ServiceConfigTest, Parser1BasicTest2) {
  const char* test_json = "{\"global_param\":1000}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  EXPECT_TRUE((static_cast<TestParsedObject1*>(
                   svc_cfg->GetParsedGlobalServiceConfigObject(0)))
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ServiceConfigTest, Parser2BasicTest) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":5}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* vector_ptr = svc_cfg->GetMethodServiceConfigObjectsVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  EXPECT_TRUE(vector_ptr != nullptr);
  auto parsed_object = ((*vector_ptr)[1]).get();
  EXPECT_TRUE(static_cast<TestParsedObject1*>(parsed_object)->value() == 5);
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
}

// Test parsing with ErrorParsers which always add errors
class ErroredParsersScopingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfig::Shutdown();
    ServiceConfig::Init();
    EXPECT_TRUE(ServiceConfig::RegisterParser(
                    UniquePtr<ServiceConfigParser>(New<ErrorParser>())) == 0);
    EXPECT_TRUE(ServiceConfig::RegisterParser(
                    UniquePtr<ServiceConfigParser>(New<ErrorParser>())) == 1);
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
}

class ClientChannelParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfig::Shutdown();
    ServiceConfig::Init();
    EXPECT_TRUE(
        ServiceConfig::RegisterParser(UniquePtr<ServiceConfigParser>(
            New<grpc_core::internal::ClientChannelServiceConfigParser>())) ==
        0);
  }
};

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigPickFirst) {
  const char* test_json = "{\"loadBalancingConfig\": [{\"pick_first\":{}}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_object =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedObject*>(
          svc_cfg->GetParsedGlobalServiceConfigObject(0));
  const auto* lb_config = parsed_object->parsed_lb_config();
  EXPECT_TRUE(strcmp(lb_config->name(), "pick_first") == 0);
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigRoundRobin) {
  const char* test_json =
      "{\"loadBalancingConfig\": [{\"round_robin\":{}}, {}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_object =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedObject*>(
          svc_cfg->GetParsedGlobalServiceConfigObject(0));
  const auto* lb_config = parsed_object->parsed_lb_config();
  EXPECT_TRUE(strcmp(lb_config->name(), "round_robin") == 0);
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfigGrpclb) {
  const char* test_json =
      "{\"loadBalancingConfig\": "
      "[{\"grpclb\":{\"childPolicy\":[{\"pick_first\":{}}]}}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_object =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedObject*>(
          svc_cfg->GetParsedGlobalServiceConfigObject(0));
  const auto* lb_config = parsed_object->parsed_lb_config();
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
  const auto* parsed_object =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedObject*>(
          svc_cfg->GetParsedGlobalServiceConfigObject(0));
  const auto* lb_config = parsed_object->parsed_lb_config();
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ClientChannelParserTest, InvalidgRPCLbLoadBalancingConfig) {
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ClientChannelParserTest, InalidLoadBalancingConfigXds) {
  const char* test_json =
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"does_not_exist\":{} },\n"
      "    { \"xds_experimental\":{} }\n"
      "  ]\n"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(
      std::string("(Service config parsing "
                  "error)(.*)(referenced_errors)(.*)(Global "
                  "Params)(.*)(referenced_errors)(.*)(Client channel global "
                  "parser)(.*)(referenced_errors)(.*)(Xds "
                  "Parser)(.*)(referenced_errors)(.*)(field:balancerName "
                  "error:not found)"));
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ClientChannelParserTest, ValidLoadBalancingPolicy) {
  const char* test_json = "{\"loadBalancingPolicy\":\"pick_first\"}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_object =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedObject*>(
          svc_cfg->GetParsedGlobalServiceConfigObject(0));
  const auto* lb_policy = parsed_object->parsed_deprecated_lb_policy();
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
}

TEST_F(ClientChannelParserTest, LoadBalancingPolicyXdsNotAllowed) {
  const char* test_json = "{\"loadBalancingPolicy\":\"xds_experimental\"}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  std::regex e(std::string(
      "(Service config parsing "
      "error)(.*)(referenced_errors)(.*)(Global "
      "Params)(.*)(referenced_errors)(.*)(Client channel global "
      "parser)(.*)(referenced_errors)(.*)(field:loadBalancingPolicy error:Xds "
      "Parser has required field - balancerName. Please use "
      "loadBalancingConfig instead of the deprecated loadBalancingPolicy)"));
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
  const auto* parsed_object =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedObject*>(
          svc_cfg->GetParsedGlobalServiceConfigObject(0));
  const auto retryThrottling = parsed_object->retry_throttling();
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* vector_ptr = svc_cfg->GetMethodServiceConfigObjectsVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  EXPECT_TRUE(vector_ptr != nullptr);
  auto parsed_object = ((*vector_ptr)[0]).get();
  EXPECT_EQ((static_cast<grpc_core::internal::ClientChannelMethodParsedObject*>(
                 parsed_object))
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
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
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* vector_ptr = svc_cfg->GetMethodServiceConfigObjectsVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  EXPECT_TRUE(vector_ptr != nullptr);
  auto parsed_object = ((*vector_ptr)[0]).get();
  EXPECT_TRUE(
      (static_cast<grpc_core::internal::ClientChannelMethodParsedObject*>(
           parsed_object))
          ->wait_for_ready()
          .has_value());
  EXPECT_TRUE(
      (static_cast<grpc_core::internal::ClientChannelMethodParsedObject*>(
           parsed_object))
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
  const auto* vector_ptr = svc_cfg->GetMethodServiceConfigObjectsVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  EXPECT_TRUE(vector_ptr != nullptr);
  const auto* parsed_object =
      static_cast<grpc_core::internal::ClientChannelMethodParsedObject*>(
          ((*vector_ptr)[0]).get());
  EXPECT_TRUE(parsed_object->retry_policy() != nullptr);
  EXPECT_EQ(parsed_object->retry_policy()->max_attempts, 3);
  EXPECT_EQ(parsed_object->retry_policy()->initial_backoff, 1000);
  EXPECT_EQ(parsed_object->retry_policy()->max_backoff, 120000);
  EXPECT_EQ(parsed_object->retry_policy()->backoff_multiplier, 1.6f);
  EXPECT_TRUE(parsed_object->retry_policy()->retryable_status_codes.Contains(
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
      "*)(field:maxAttempts error:should be atleast 2)"));
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
      "      \"maxBackoff\": \"120sec\",\n"
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
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
      "      \"maxBackoff\": \"120sec\",\n"
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
}

class MessageSizeParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfig::Shutdown();
    ServiceConfig::Init();
    EXPECT_TRUE(ServiceConfig::RegisterParser(UniquePtr<ServiceConfigParser>(
                    New<MessageSizeParser>())) == 0);
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
  const auto* vector_ptr = svc_cfg->GetMethodServiceConfigObjectsVector(
      grpc_slice_from_static_string("/TestServ/TestMethod"));
  EXPECT_TRUE(vector_ptr != nullptr);
  auto parsed_object =
      static_cast<MessageSizeParsedObject*>(((*vector_ptr)[0]).get());
  ASSERT_TRUE(parsed_object != nullptr);
  EXPECT_EQ(parsed_object->limits().max_send_size, 1024);
  EXPECT_EQ(parsed_object->limits().max_recv_size, 1024);
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
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
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
}

class HealthCheckParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServiceConfig::Shutdown();
    ServiceConfig::Init();
    EXPECT_TRUE(ServiceConfig::RegisterParser(UniquePtr<ServiceConfigParser>(
                    New<HealthCheckParser>())) == 0);
  }
};

TEST_F(HealthCheckParserTest, Valid) {
  const char* test_json =
      "{\n"
      "  \"healthCheckConfig\": {\n"
      "    \"serviceName\": \"health_check_service_name\"\n"
      "    }\n"
      "}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_object = static_cast<grpc_core::HealthCheckParsedObject*>(
      svc_cfg->GetParsedGlobalServiceConfigObject(0));
  ASSERT_TRUE(parsed_object != nullptr);
  EXPECT_EQ(strcmp(parsed_object->service_name(), "health_check_service_name"),
            0);
}

TEST_F(HealthCheckParserTest, MultipleEntries) {
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
                  "field:serviceName error:Duplicate entry)"));
  std::smatch match;
  std::string s(grpc_error_string(error));
  EXPECT_TRUE(std::regex_search(s, match, e));
  GRPC_ERROR_UNREF(error);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
