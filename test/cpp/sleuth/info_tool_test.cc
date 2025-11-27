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
#include "test/cpp/sleuth/version.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"

namespace grpc_sleuth {
namespace {

TEST(InfoToolTest, PrintVersion) {
  auto result = TestTool("info", {});
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value(),
            absl::StrCat("Sleuth version ", kSleuthVersion, "\n"));
}

}  // namespace
}  // namespace grpc_sleuth
