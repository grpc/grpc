/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/gprpp/string_view.h"
#include <grpc/slice.h>

#include <gtest/gtest.h>
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

TEST(StringViewTest, Empty) {
  grpc_core::StringView empty;
  EXPECT_TRUE(empty.empty());
  EXPECT_EQ(empty.size(), 0lu);

  grpc_core::StringView empty_buf("");
  EXPECT_TRUE(empty_buf.empty());
  EXPECT_EQ(empty_buf.size(), 0lu);

  grpc_core::StringView empty_trimmed("foo", 0);
  EXPECT_TRUE(empty_trimmed.empty());
  EXPECT_EQ(empty_trimmed.size(), 0lu);

  grpc_core::StringView empty_slice(grpc_empty_slice());
  EXPECT_TRUE(empty_slice.empty());
  EXPECT_EQ(empty_slice.size(), 0lu);
}

TEST(StringViewTest, Size) {
  constexpr char kStr[] = "foo";
  grpc_core::StringView str1(kStr);
  EXPECT_EQ(str1.size(), strlen(kStr));
  grpc_core::StringView str2(kStr, 2);
  EXPECT_EQ(str2.size(), 2lu);
}

TEST(StringViewTest, Data) {
  constexpr char kStr[] = "foo-bar";
  grpc_core::StringView str(kStr);
  EXPECT_EQ(str.size(), strlen(kStr));
  for (size_t i = 0; i < strlen(kStr); ++i) {
    EXPECT_EQ(str[i], kStr[i]);
  }
}

TEST(StringViewTest, Slice) {
  constexpr char kStr[] = "foo";
  grpc_core::StringView slice(grpc_slice_from_static_string(kStr));
  EXPECT_EQ(slice.size(), strlen(kStr));
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
