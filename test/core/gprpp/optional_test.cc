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

#include "src/core/lib/gprpp/optional.h"
#include <grpc/support/log.h>
#include <gtest/gtest.h>
#include "src/core/lib/gprpp/memory.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

namespace {
TEST(OptionalTest, BasicTest) {
  grpc_core::Optional<int> opt_val;
  EXPECT_FALSE(opt_val.has_value());
  const int kTestVal = 123;

  opt_val.set(kTestVal);
  EXPECT_TRUE(opt_val.has_value());
  EXPECT_EQ(opt_val.value(), kTestVal);

  opt_val.reset();
  EXPECT_EQ(opt_val.has_value(), false);
}
}  // namespace

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
