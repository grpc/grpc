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
#include "src/core/lib/gprpp/global_config.h"
#include "src/core/lib/gprpp/memory.h"

GPR_GLOBAL_CONFIG_DECLARE_BOOL(bool_var);

GPR_GLOBAL_CONFIG_DEFINE_BOOL(bool_var, false, "");
GPR_GLOBAL_CONFIG_DEFINE_INT32(int32_var, 0, "");
GPR_GLOBAL_CONFIG_DEFINE_STRING(string_var, "", "");

TEST(GlobalConfigTest, BoolTest) {
  EXPECT_FALSE(GPR_GLOBAL_CONFIG_GET(bool_var));
  GPR_GLOBAL_CONFIG_SET(bool_var, true);
  EXPECT_TRUE(GPR_GLOBAL_CONFIG_GET(bool_var));
}

TEST(GlobalConfigTest, Int32Test) {
  EXPECT_EQ(0, GPR_GLOBAL_CONFIG_GET(int32_var));
  GPR_GLOBAL_CONFIG_SET(int32_var, 1024);
  EXPECT_EQ(1024, GPR_GLOBAL_CONFIG_GET(int32_var));
}

TEST(GlobalConfigTest, StringTest) {
  grpc_core::UniquePtr<char> value;

  value = GPR_GLOBAL_CONFIG_GET(string_var);
  EXPECT_EQ(0, strcmp(value.get(), ""));

  GPR_GLOBAL_CONFIG_SET(string_var, "Test");

  value = GPR_GLOBAL_CONFIG_GET(string_var);
  EXPECT_EQ(0, strcmp(value.get(), "Test"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
