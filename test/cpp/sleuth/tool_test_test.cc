// Copyright 2025 gRPC authors.
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

#include "test/cpp/sleuth/tool_test.h"

#include <cstdint>
#include <string>
#include <vector>

#include "test/cpp/sleuth/tool.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_sleuth {
namespace {

using ::testing::HasSubstr;
using ::testing::status::IsOkAndHolds;
using ::testing::status::StatusIs;

SLEUTH_TOOL(test_tool, "", "A test tool.") {
  GetPrintFn()("Hello, world!\n");
  return absl::OkStatus();
}

TEST(ToolTest, TestTool) {
  auto result = TestTool("test_tool", {});
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, "Hello, world!\n");
}

TEST(ToolArgsTest, TryCreateSuccess) {
  EXPECT_OK(ToolArgs::TryCreate({}));
  EXPECT_OK(ToolArgs::TryCreate({"a=b"}));
  EXPECT_OK(ToolArgs::TryCreate({"a=b", "c=d"}));
  EXPECT_OK(ToolArgs::TryCreate({"a="}));
  EXPECT_OK(ToolArgs::TryCreate({"a=b=c"}));
}

TEST(ToolArgsTest, TryCreateFailure) {
  EXPECT_THAT(ToolArgs::TryCreate({"a"}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid argument format")));
  EXPECT_THAT(ToolArgs::TryCreate({"=b"}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Key cannot be empty")));
  EXPECT_THAT(ToolArgs::TryCreate({"a=b", "a=c"}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Duplicate key: a")));
}

TEST(ToolArgsTest, TryGetFlag) {
  auto args = ToolArgs::TryCreate({"a=b", "i=123", "d=1.23"});
  ASSERT_OK(args);
  EXPECT_THAT((*args)->TryGetFlag<std::string>("a"), IsOkAndHolds("b"));
  EXPECT_THAT((*args)->TryGetFlag<int64_t>("i"), IsOkAndHolds(123));
  EXPECT_THAT((*args)->TryGetFlag<double>("d"), IsOkAndHolds(1.23));
  EXPECT_THAT((*args)->TryGetFlag<std::string>("missing"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("missing is required")));
  EXPECT_THAT((*args)->TryGetFlag<std::string>("missing", "default"),
              IsOkAndHolds("default"));
  EXPECT_THAT((*args)->TryGetFlag<int64_t>("a"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("not an integer")));
  EXPECT_THAT((*args)->TryGetFlag<double>("a"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("not a double")));
}

}  // namespace
}  // namespace grpc_sleuth
