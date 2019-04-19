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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gprpp/global_config.h"
#include "src/core/lib/gprpp/memory.h"
#include "test/core/util/test_config.h"

#define LOG_TEST_NAME(x) gpr_log(GPR_INFO, "%s", x)

static bool g_config_error_function_called;

static void clear_config_error_called() {
  g_config_error_function_called = false;
}

static bool is_config_error_called() { return g_config_error_function_called; }

// This function is for preventing the program from halt due to
// configuration error and make test routines know whether there is error.
static void fake_config_error_function(const char* error_message) {
  g_config_error_function_called = true;
}

GPR_GLOBAL_CONFIG_DECLARE_BOOL(bool_var);

GPR_GLOBAL_CONFIG_DEFINE_BOOL(bool_var, false, "");
GPR_GLOBAL_CONFIG_DEFINE_INT32(int32_var, 0, "");
GPR_GLOBAL_CONFIG_DEFINE_STRING(string_var, "", "");

static void test_bool(void) {
  LOG_TEST_NAME("test_bool");

  clear_config_error_called();

  GPR_ASSERT(GPR_GLOBAL_CONFIG_GET(bool_var) == false);
  GPR_GLOBAL_CONFIG_SET(bool_var, true);
  GPR_ASSERT(GPR_GLOBAL_CONFIG_GET(bool_var) == true);

  GPR_ASSERT(is_config_error_called() == false);
}

static void test_int32(void) {
  LOG_TEST_NAME("test_int32");

  clear_config_error_called();

  GPR_ASSERT(GPR_GLOBAL_CONFIG_GET(int32_var) == 0);
  GPR_GLOBAL_CONFIG_SET(int32_var, 1024);
  GPR_ASSERT(GPR_GLOBAL_CONFIG_GET(int32_var) == 1024);

  GPR_ASSERT(is_config_error_called() == false);
}

static void test_string(void) {
  LOG_TEST_NAME("test_string");

  grpc_core::UniquePtr<char> value;

  clear_config_error_called();

  value = GPR_GLOBAL_CONFIG_GET(string_var);
  GPR_ASSERT(strcmp(value.get(), "") == 0);

  GPR_GLOBAL_CONFIG_SET(string_var, "Test");

  value = GPR_GLOBAL_CONFIG_GET(string_var);
  GPR_ASSERT(strcmp(value.get(), "Test") == 0);

  GPR_ASSERT(is_config_error_called() == false);
}

GPR_GLOBAL_CONFIG_DEFINE_BOOL(bool_var_2, true, "");
GPR_GLOBAL_CONFIG_DEFINE_INT32(int32_var_2, 1234, "");
GPR_GLOBAL_CONFIG_DEFINE_STRING(string_var_2, "Apple", "");

static void test_bool_with_env(void) {
  LOG_TEST_NAME("test_bool_with_env");

  const char* bool_var_2_name = "BOOL_VAR_2";

  clear_config_error_called();

  gpr_unsetenv(bool_var_2_name);
  GPR_ASSERT(GPR_GLOBAL_CONFIG_GET(bool_var_2) == true);

  gpr_setenv(bool_var_2_name, "");
  GPR_ASSERT(GPR_GLOBAL_CONFIG_GET(bool_var_2) == true);

  gpr_setenv(bool_var_2_name, "true");
  GPR_ASSERT(GPR_GLOBAL_CONFIG_GET(bool_var_2) == true);

  gpr_setenv(bool_var_2_name, "false");
  GPR_ASSERT(GPR_GLOBAL_CONFIG_GET(bool_var_2) == false);

  GPR_ASSERT(is_config_error_called() == false);

  gpr_setenv(bool_var_2_name, "!");
  GPR_GLOBAL_CONFIG_GET(bool_var_2);
  GPR_ASSERT(is_config_error_called() == true);
}

static void test_int32_with_env(void) {
  LOG_TEST_NAME("test_int32_with_env");

  const char* int32_var_2_name = "INT32_VAR_2";

  clear_config_error_called();

  gpr_unsetenv(int32_var_2_name);
  GPR_ASSERT(GPR_GLOBAL_CONFIG_GET(int32_var_2) == 1234);

  gpr_setenv(int32_var_2_name, "0");
  GPR_ASSERT(GPR_GLOBAL_CONFIG_GET(int32_var_2) == 0);

  gpr_setenv(int32_var_2_name, "-123456789");
  GPR_ASSERT(GPR_GLOBAL_CONFIG_GET(int32_var_2) == -123456789);

  gpr_setenv(int32_var_2_name, "123456789");
  GPR_ASSERT(GPR_GLOBAL_CONFIG_GET(int32_var_2) == 123456789);

  GPR_ASSERT(is_config_error_called() == false);

  gpr_setenv(int32_var_2_name, "-1AB");
  GPR_GLOBAL_CONFIG_GET(int32_var_2);
  GPR_ASSERT(is_config_error_called() == true);
}

static void test_string_with_env(void) {
  LOG_TEST_NAME("test_string_with_env");

  const char* string_var_2_name = "STRING_VAR_2";
  grpc_core::UniquePtr<char> value;

  clear_config_error_called();

  gpr_unsetenv(string_var_2_name);
  value = GPR_GLOBAL_CONFIG_GET(string_var_2);
  GPR_ASSERT(strcmp(value.get(), "Apple") == 0);

  gpr_setenv(string_var_2_name, "Banana");
  value = GPR_GLOBAL_CONFIG_GET(string_var_2);
  GPR_ASSERT(strcmp(value.get(), "Banana") == 0);

  gpr_setenv(string_var_2_name, "");
  value = GPR_GLOBAL_CONFIG_GET(string_var_2);
  GPR_ASSERT(strcmp(value.get(), "") == 0);

  GPR_ASSERT(is_config_error_called() == false);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);

  // Not to abort the test when parsing error happens.
  gpr_set_global_config_error_function(&fake_config_error_function);

  test_bool();
  test_int32();
  test_string();

  // Following tests work only with the default config system.
#ifndef GPR_GLOBAL_CONFIG_CUSTOM
  test_bool_with_env();
  test_int32_with_env();
  test_string_with_env();
#endif
  return 0;
}
