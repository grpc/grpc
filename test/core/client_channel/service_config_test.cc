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

#include <gtest/gtest.h>

#include <grpc/grpc.h>
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
          *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "global_param value type should be a number");
          return nullptr;
        }
        int value = gpr_parse_nonnegative_int(field->value);
        if (value == -1) {
          *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "global_param value type should be non-negative");
          return nullptr;
        }
        return UniquePtr<ServiceConfigParsedObject>(
            New<TestParsedObject1>(value));
      }
    }
    return nullptr;
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
          *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "method_param value type should be a number");
          return nullptr;
        }
        int value = gpr_parse_nonnegative_int(field->value);
        if (value == -1) {
          *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "method_param value type should be non-negative");
          return nullptr;
        }
        return UniquePtr<ServiceConfigParsedObject>(
            New<TestParsedObject1>(value));
      }
    }
    return nullptr;
  }
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
  EXPECT_TRUE(error != GRPC_ERROR_NONE);
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
  EXPECT_TRUE(error != GRPC_ERROR_NONE);
}

TEST_F(ServiceConfigTest, ErrorNoNamesWithMultipleMethodConfigs) {
  const char* test_json =
      "{\"methodConfig\": [{}, {\"name\":[{\"service\":\"TestServ\"}]}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  EXPECT_TRUE(error != GRPC_ERROR_NONE);
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
  EXPECT_TRUE(error == GRPC_ERROR_NONE);
  EXPECT_TRUE((static_cast<TestParsedObject1*>(
                   svc_cfg->GetParsedGlobalServiceConfigObject(0)))
                  ->value() == 5);
}

TEST_F(ServiceConfigTest, Parser1BasicTest2) {
  const char* test_json = "{\"global_param\":1000}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  EXPECT_TRUE(error == GRPC_ERROR_NONE);
  EXPECT_TRUE((static_cast<TestParsedObject1*>(
                   svc_cfg->GetParsedGlobalServiceConfigObject(0)))
                  ->value() == 1000);
}

TEST_F(ServiceConfigTest, Parser1ErrorInvalidType) {
  const char* test_json = "{\"global_param\":\"5\"}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  EXPECT_TRUE(error != GRPC_ERROR_NONE);
}

TEST_F(ServiceConfigTest, Parser1ErrorInvalidValue) {
  const char* test_json = "{\"global_param\":-5}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
  EXPECT_TRUE(error != GRPC_ERROR_NONE);
}

TEST_F(ServiceConfigTest, Parser2BasicTest) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":5}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  EXPECT_TRUE(error == GRPC_ERROR_NONE);
}

TEST_F(ServiceConfigTest, Parser2ErrorInvalidType) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":\"5\"}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  EXPECT_TRUE(error != GRPC_ERROR_NONE);
}

TEST_F(ServiceConfigTest, Parser2ErrorInvalidValue) {
  const char* test_json =
      "{\"methodConfig\": [{\"name\":[{\"service\":\"TestServ\"}], "
      "\"method_param\":-5}]}";
  grpc_error* error = GRPC_ERROR_NONE;
  auto svc_cfg = ServiceConfig::Create(test_json, &error);
  EXPECT_TRUE(error != GRPC_ERROR_NONE);
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
