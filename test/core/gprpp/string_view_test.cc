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

#include <grpc/slice.h>

#include "src/core/lib/gprpp/string_view.h"

#include <gtest/gtest.h>
#include "src/core/lib/gprpp/memory.h"
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

  grpc_core::StringView empty_slice(
      grpc_core::StringViewFromSlice(grpc_empty_slice()));
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
  grpc_core::StringView slice(
      grpc_core::StringViewFromSlice(grpc_slice_from_static_string(kStr)));
  EXPECT_EQ(slice.size(), strlen(kStr));
}

TEST(StringViewTest, Dup) {
  constexpr char kStr[] = "foo";
  grpc_core::StringView slice(
      grpc_core::StringViewFromSlice(grpc_slice_from_static_string(kStr)));
  grpc_core::UniquePtr<char> dup = grpc_core::StringViewToCString(slice);
  EXPECT_EQ(0, strcmp(kStr, dup.get()));
  EXPECT_EQ(slice.size(), strlen(kStr));
}

TEST(StringViewTest, Eq) {
  constexpr char kStr1[] = "foo";
  constexpr char kStr2[] = "bar";
  grpc_core::StringView str1(kStr1);
  EXPECT_EQ(kStr1, str1);
  EXPECT_EQ(str1, kStr1);
  grpc_core::StringView slice1(
      grpc_core::StringViewFromSlice(grpc_slice_from_static_string(kStr1)));
  EXPECT_EQ(slice1, str1);
  EXPECT_EQ(str1, slice1);
  EXPECT_NE(slice1, kStr2);
  EXPECT_NE(kStr2, slice1);
  grpc_core::StringView slice2(
      grpc_core::StringViewFromSlice(grpc_slice_from_static_string(kStr2)));
  EXPECT_NE(slice2, str1);
  EXPECT_NE(str1, slice2);
}

TEST(StringViewTest, Cmp) {
  constexpr char kStr1[] = "abc";
  constexpr char kStr2[] = "abd";
  constexpr char kStr3[] = "abcd";
  grpc_core::StringView str1(kStr1);
  grpc_core::StringView str2(kStr2);
  grpc_core::StringView str3(kStr3);
  EXPECT_EQ(grpc_core::StringViewCmp(str1, str1), 0);
  EXPECT_LT(grpc_core::StringViewCmp(str1, str2), 0);
  EXPECT_LT(grpc_core::StringViewCmp(str1, str3), 0);
  EXPECT_EQ(grpc_core::StringViewCmp(str2, str2), 0);
  EXPECT_GT(grpc_core::StringViewCmp(str2, str1), 0);
  EXPECT_GT(grpc_core::StringViewCmp(str2, str3), 0);
  EXPECT_EQ(grpc_core::StringViewCmp(str3, str3), 0);
  EXPECT_GT(grpc_core::StringViewCmp(str3, str1), 0);
  EXPECT_LT(grpc_core::StringViewCmp(str3, str2), 0);
}

TEST(StringViewTest, RemovePrefix) {
  constexpr char kStr[] = "abcd";
  grpc_core::StringView str(kStr);
  str.remove_prefix(1);
  EXPECT_EQ("bcd", str);
  str.remove_prefix(2);
  EXPECT_EQ("d", str);
  str.remove_prefix(1);
  EXPECT_EQ("", str);
}

TEST(StringViewTest, RemoveSuffix) {
  constexpr char kStr[] = "abcd";
  grpc_core::StringView str(kStr);
  str.remove_suffix(1);
  EXPECT_EQ("abc", str);
  str.remove_suffix(2);
  EXPECT_EQ("a", str);
  str.remove_suffix(1);
  EXPECT_EQ("", str);
}

TEST(StringViewTest, Substring) {
  constexpr char kStr[] = "abcd";
  grpc_core::StringView str(kStr);
  EXPECT_EQ("bcd", str.substr(1));
  EXPECT_EQ("bc", str.substr(1, 2));
}

TEST(StringViewTest, Find) {
  // Passing StringView::npos directly to GTEST macros result in link errors.
  // Store the value in a local variable and use it in the test.
  const size_t npos = grpc_core::StringView::npos;
  constexpr char kStr[] = "abacad";
  grpc_core::StringView str(kStr);
  EXPECT_EQ(0ul, str.find('a'));
  EXPECT_EQ(2ul, str.find('a', 1));
  EXPECT_EQ(4ul, str.find('a', 3));
  EXPECT_EQ(1ul, str.find('b'));
  EXPECT_EQ(npos, str.find('b', 2));
  EXPECT_EQ(npos, str.find('z'));
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
