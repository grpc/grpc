/*
 *
 * Copyright 2020 the gRPC authors.
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

#include "test/core/util/stack_tracer.h"

#include <gtest/gtest.h>
#include <string>

#include "absl/debugging/symbolize.h"
#include "absl/strings/match.h"

#include <grpc/support/log.h>

#include "test/core/util/test_config.h"

TEST(StackTracerTest, Basic) {
  std::string stack_trace = grpc_core::testing::GetCurrentStackTrace();
  gpr_log(GPR_INFO, "stack_trace=%s", stack_trace.c_str());
#if !defined(NDEBUG) && !defined(GPR_MUSL_LIBC_COMPAT)
  EXPECT_TRUE(absl::StrContains(stack_trace, "Basic"));
#endif
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
