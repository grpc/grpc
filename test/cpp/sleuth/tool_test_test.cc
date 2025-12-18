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

SLEUTH_TOOL(test_tool, "", "A test tool.") {
  print_fn("Hello, world!\n");
  return absl::OkStatus();
}

TEST(ToolTest, TestTool) {
  auto result = TestTool("test_tool", {});
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, "Hello, world!\n");
}

TEST(ToolArgsTest, TryCreateSuccess) {
  EXPECT_TRUE(ToolArgs::TryCreate({}).ok());
  EXPECT_TRUE(ToolArgs::TryCreate({"a=b"}).ok());
  EXPECT_TRUE(ToolArgs::TryCreate({"a=b", "c=d"}).ok());
  EXPECT_TRUE(ToolArgs::TryCreate({"a="}).ok());
  EXPECT_TRUE(ToolArgs::TryCreate({"a=b=c"}).ok());
}

TEST(ToolArgsTest, TryCreateFailure) {
  auto s1 = ToolArgs::TryCreate({"a"});
  EXPECT_EQ(s1.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(s1.status().message(), HasSubstr("Invalid argument format"));
  auto s2 = ToolArgs::TryCreate({"=b"});
  EXPECT_EQ(s2.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(s2.status().message(), HasSubstr("Key cannot be empty"));
  auto s3 = ToolArgs::TryCreate({"a=b", "a=c"});
  EXPECT_EQ(s3.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(s3.status().message(), HasSubstr("Duplicate key: a"));
}

TEST(ToolArgsTest, TryGetFlag) {
  auto args = ToolArgs::TryCreate({"a=b", "i=123", "d=1.23"});
  auto a = (*args)->TryGetFlag<std::string>("a");
  EXPECT_EQ(*a, "b");
  auto i = (*args)->TryGetFlag<int64_t>("i");
  EXPECT_EQ(*i, 123);
  auto d = (*args)->TryGetFlag<double>("d");
  EXPECT_EQ(*d, 1.23);
  auto missing = (*args)->TryGetFlag<std::string>("missing");
  EXPECT_EQ(missing.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(missing.status().message(), HasSubstr("missing is required"));
  auto missing_def = (*args)->TryGetFlag<std::string>("missing", "default");
  EXPECT_EQ(*missing_def, "default");
  auto a_int = (*args)->TryGetFlag<int64_t>("a");
  EXPECT_EQ(a_int.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(a_int.status().message(), HasSubstr("not an integer"));
  auto a_double = (*args)->TryGetFlag<double>("a");
  EXPECT_EQ(a_double.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(a_double.status().message(), HasSubstr("not a double"));
}

}  // namespace
}  // namespace grpc_sleuth
