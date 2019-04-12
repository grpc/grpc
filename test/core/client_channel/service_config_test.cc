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
  EXPECT_TRUE(strstr(grpc_error_string(error),
                     "failed to parse JSON for service config") != nullptr);
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
  EXPECT_TRUE(strstr(grpc_error_string(error), "No names found") != nullptr);
  GRPC_ERROR_UNREF(error);
}

TEST_F(ServiceConfigTest, ErrorNoNamesWithMultipleMethodConfigs) {
  const char* test_json =
      "{\"methodConfig\": [{}, {\"name\":[{\"service\":\"TestServ\"}]}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error != GRPC_ERROR_NONE);
  EXPECT_TRUE(strstr(grpc_error_string(error), "No names found") != nullptr);
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
                           "Params)(.*)(referenced_errors)()(.*)") +
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
                           "Params)(.*)(referenced_errors)()(.*)") +
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
                           "Params)(.*)(referenced_errors)()(.*)(methodConfig)("
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

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfig1) {
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

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfig2) {
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

TEST_F(ClientChannelParserTest, ValidLoadBalancingConfig3) {
  const char* test_json =
      "{\"loadBalancingConfig\": "
      "[{\"grpclb\":{\"childPolicy\":[{\"pick_first\":{}}]}}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  ASSERT_TRUE(error == GRPC_ERROR_NONE);
  const auto* parsed_object =
      static_cast<grpc_core::internal::ClientChannelGlobalParsedObject*>(
          svc_cfg->GetParsedGlobalServiceConfigObject(0));
  const auto* lb_config = parsed_object->parsed_lb_config();
  EXPECT_TRUE(strcmp(lb_config->name(), "grpclb") == 0);
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
