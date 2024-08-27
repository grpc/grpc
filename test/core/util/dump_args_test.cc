// Copyright 2024 gRPC authors.
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

#include "src/core/util/dump_args.h"

#include <sstream>

#include "gtest/gtest.h"

int add(int a, int b) { return a + b; }

TEST(DumpArgsTest, Basic) {
  int a = 1;
  int b = 2;
  int c = 3;
  EXPECT_EQ("a = 1, b = 2, c = 3", absl::StrCat(GRPC_DUMP_ARGS(a, b, c)));
}

TEST(DumpArgsTest, FunctionCall) {
  EXPECT_EQ("add(1, 2) = 3", absl::StrCat(GRPC_DUMP_ARGS(add(1, 2))));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
