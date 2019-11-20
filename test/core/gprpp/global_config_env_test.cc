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

#include <stdio.h>
#include <string.h>

#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gprpp/global_config_env.h"
#include "src/core/lib/gprpp/memory.h"

namespace {

bool g_config_error_function_called;

void ClearConfigErrorCalled() { g_config_error_function_called = false; }

bool IsConfigErrorCalled() { return g_config_error_function_called; }

// This function is for preventing the program from invoking
// an error handler due to configuration error and
// make test routines know whether there is error.
void FakeConfigErrorFunction(const char* /*error_message*/) {
  g_config_error_function_called = true;
}

class GlobalConfigEnvTest : public ::testing::Test {
 protected:
  void SetUp() override { ClearConfigErrorCalled(); }
  void TearDown() override { EXPECT_FALSE(IsConfigErrorCalled()); }
};

}  // namespace

GPR_GLOBAL_CONFIG_DEFINE_BOOL(bool_var, true, "");
GPR_GLOBAL_CONFIG_DEFINE_INT32(int32_var, 1234, "");
GPR_GLOBAL_CONFIG_DEFINE_STRING(string_var, "Apple", "");

TEST_F(GlobalConfigEnvTest, BoolWithEnvTest) {
  const char* bool_var_name = "BOOL_VAR";

  gpr_unsetenv(bool_var_name);
  EXPECT_TRUE(GPR_GLOBAL_CONFIG_GET(bool_var));

  gpr_setenv(bool_var_name, "true");
  EXPECT_TRUE(GPR_GLOBAL_CONFIG_GET(bool_var));

  gpr_setenv(bool_var_name, "false");
  EXPECT_FALSE(GPR_GLOBAL_CONFIG_GET(bool_var));

  EXPECT_FALSE(IsConfigErrorCalled());

  gpr_setenv(bool_var_name, "");
  GPR_GLOBAL_CONFIG_GET(bool_var);
  EXPECT_TRUE(IsConfigErrorCalled());
  ClearConfigErrorCalled();

  gpr_setenv(bool_var_name, "!");
  GPR_GLOBAL_CONFIG_GET(bool_var);
  EXPECT_TRUE(IsConfigErrorCalled());
  ClearConfigErrorCalled();
}

TEST_F(GlobalConfigEnvTest, Int32WithEnvTest) {
  const char* int32_var_name = "INT32_VAR";

  gpr_unsetenv(int32_var_name);
  EXPECT_EQ(1234, GPR_GLOBAL_CONFIG_GET(int32_var));

  gpr_setenv(int32_var_name, "0");
  EXPECT_EQ(0, GPR_GLOBAL_CONFIG_GET(int32_var));

  gpr_setenv(int32_var_name, "-123456789");
  EXPECT_EQ(-123456789, GPR_GLOBAL_CONFIG_GET(int32_var));

  gpr_setenv(int32_var_name, "123456789");
  EXPECT_EQ(123456789, GPR_GLOBAL_CONFIG_GET(int32_var));

  EXPECT_FALSE(IsConfigErrorCalled());

  gpr_setenv(int32_var_name, "-1AB");
  GPR_GLOBAL_CONFIG_GET(int32_var);
  EXPECT_TRUE(IsConfigErrorCalled());
  ClearConfigErrorCalled();
}

TEST_F(GlobalConfigEnvTest, StringWithEnvTest) {
  const char* string_var_name = "STRING_VAR";
  grpc_core::UniquePtr<char> value;

  gpr_unsetenv(string_var_name);
  value = GPR_GLOBAL_CONFIG_GET(string_var);
  EXPECT_EQ(0, strcmp(value.get(), "Apple"));

  gpr_setenv(string_var_name, "Banana");
  value = GPR_GLOBAL_CONFIG_GET(string_var);
  EXPECT_EQ(0, strcmp(value.get(), "Banana"));

  gpr_setenv(string_var_name, "");
  value = GPR_GLOBAL_CONFIG_GET(string_var);
  EXPECT_EQ(0, strcmp(value.get(), ""));
}

int main(int argc, char** argv) {
  // Not to abort the test when parsing error happens.
  grpc_core::SetGlobalConfigEnvErrorFunction(&FakeConfigErrorFunction);

  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
