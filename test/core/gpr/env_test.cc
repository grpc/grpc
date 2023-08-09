//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "src/core/lib/gprpp/env.h"

#include "gtest/gtest.h"

#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

#define LOG_TEST_NAME(x) gpr_log(GPR_INFO, "%s", x)

TEST(EnvTest, SetenvGetenv) {
  const char* name = "FOO";
  const char* value = "BAR";

  LOG_TEST_NAME("test_setenv_getenv");

  grpc_core::SetEnv(name, value);
  auto retrieved_value = grpc_core::GetEnv(name);
  ASSERT_EQ(value, retrieved_value);
}

TEST(EnvTest, Unsetenv) {
  const char* name = "FOO";
  const char* value = "BAR";

  LOG_TEST_NAME("test_unsetenv");

  grpc_core::SetEnv(name, value);
  grpc_core::UnsetEnv(name);
  auto retrieved_value = grpc_core::GetEnv(name);
  ASSERT_FALSE(retrieved_value.has_value());
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
