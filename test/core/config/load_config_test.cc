// Copyright 2023 gRPC authors.
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

#include "src/core/lib/config/load_config.h"

#include "absl/flags/flag.h"
#include "gtest/gtest.h"

#include "src/core/lib/gprpp/env.h"

ABSL_FLAG(std::vector<std::string>, comma_separated_strings, {}, "");

namespace grpc_core {

TEST(LoadConfigTest, LoadCommaSeparated) {
  SetEnv("grpc_comma_separated_strings", "foo");
  EXPECT_EQ(LoadConfig(FLAGS_comma_separated_strings,
                       "grpc_comma_separated_strings", {}, ""),
            "foo");
  EXPECT_EQ(LoadConfig(FLAGS_comma_separated_strings,
                       "grpc_comma_separated_strings", "bar", ""),
            "bar");
  absl::SetFlag(&FLAGS_comma_separated_strings, {"hello"});
  EXPECT_EQ(LoadConfig(FLAGS_comma_separated_strings,
                       "grpc_comma_separated_strings", {}, ""),
            "hello");
  EXPECT_EQ(LoadConfig(FLAGS_comma_separated_strings,
                       "grpc_comma_separated_strings", "bar", ""),
            "bar");
  absl::SetFlag(&FLAGS_comma_separated_strings, {"hello", "world"});
  EXPECT_EQ(LoadConfig(FLAGS_comma_separated_strings,
                       "grpc_comma_separated_strings", {}, ""),
            "hello,world");
  EXPECT_EQ(LoadConfig(FLAGS_comma_separated_strings,
                       "grpc_comma_separated_strings", "bar", ""),
            "bar");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
