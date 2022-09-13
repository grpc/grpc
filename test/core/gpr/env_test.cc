/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/gpr/env.h"

#include "gtest/gtest.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

#define LOG_TEST_NAME(x) gpr_log(GPR_INFO, "%s", x)

TEST(EnvTest, SetenvGetenv) {
  const char* name = "FOO";
  const char* value = "BAR";
  char* retrieved_value;

  LOG_TEST_NAME("test_setenv_getenv");

  gpr_setenv(name, value);
  retrieved_value = gpr_getenv(name);
  ASSERT_NE(retrieved_value, nullptr);
  ASSERT_STREQ(value, retrieved_value);
  gpr_free(retrieved_value);
}

TEST(EnvTest, Unsetenv) {
  const char* name = "FOO";
  const char* value = "BAR";
  char* retrieved_value;

  LOG_TEST_NAME("test_unsetenv");

  gpr_setenv(name, value);
  gpr_unsetenv(name);
  retrieved_value = gpr_getenv(name);
  ASSERT_EQ(retrieved_value, nullptr);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
