//
// Copyright 2026 gRPC authors.
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

#include "src/core/util/ref_counted_string.h"

#include <string.h>

#include <map>
#include <string>

#include "src/core/util/ref_counted_ptr.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

TEST(RefCountedStringTest, MakeCopiesContents) {
  std::string source = "hello world";
  RefCountedPtr<RefCountedString> str = RefCountedString::Make(source);
  // Mutating the source must not affect the stored copy.
  source[0] = 'J';
  EXPECT_EQ(str->as_string_view(), "hello world");
}

TEST(RefCountedStringTest, IsNullTerminated) {
  RefCountedPtr<RefCountedString> str = RefCountedString::Make("abc");
  EXPECT_EQ(strlen(str->c_str()), 3u);
  EXPECT_STREQ(str->c_str(), "abc");
}

TEST(RefCountedStringTest, EmptyString) {
  RefCountedPtr<RefCountedString> str = RefCountedString::Make("");
  EXPECT_TRUE(str->as_string_view().empty());
  EXPECT_STREQ(str->c_str(), "");
}

TEST(RefCountedStringTest, EmbeddedNulIsPreservedInStringView) {
  const absl::string_view input("a\0b", 3);
  RefCountedPtr<RefCountedString> str = RefCountedString::Make(input);
  EXPECT_EQ(str->as_string_view().size(), 3u);
  EXPECT_EQ(str->as_string_view(), input);
}

TEST(RefCountedStringTest, RefSharesTheSameObject) {
  RefCountedPtr<RefCountedString> str = RefCountedString::Make("shared");
  RefCountedPtr<RefCountedString> other = str->Ref();
  EXPECT_EQ(str.get(), other.get());
  // Dropping one reference must leave the string valid for the other.
  str.reset();
  EXPECT_EQ(other->as_string_view(), "shared");
}

TEST(RefCountedStringValueTest, DefaultConstructedIsEmpty) {
  RefCountedStringValue value;
  EXPECT_TRUE(value.as_string_view().empty());
  EXPECT_STREQ(value.c_str(), "");
  EXPECT_EQ(value, RefCountedStringValue(""));
}

TEST(RefCountedStringValueTest, ConstructedFromStringView) {
  RefCountedStringValue value("foo");
  EXPECT_EQ(value.as_string_view(), "foo");
  EXPECT_STREQ(value.c_str(), "foo");
}

TEST(RefCountedStringValueTest, CopyIsIndependentlyUsable) {
  RefCountedStringValue original("copy me");
  RefCountedStringValue copy = original;
  EXPECT_EQ(original, copy);
  original = RefCountedStringValue("something else");
  EXPECT_EQ(copy, "copy me");
  EXPECT_EQ(original, "something else");
}

TEST(RefCountedStringValueTest, EqualityOperators) {
  RefCountedStringValue a("abc");
  RefCountedStringValue b("abc");
  RefCountedStringValue c("abd");
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);
  EXPECT_TRUE(a != c);
  EXPECT_FALSE(a == c);
  // Mixed comparisons against absl::string_view, in both directions.
  EXPECT_TRUE(a == absl::string_view("abc"));
  EXPECT_TRUE(absl::string_view("abc") == a);
  EXPECT_TRUE(a != absl::string_view("xyz"));
  EXPECT_TRUE(absl::string_view("xyz") != a);
}

TEST(RefCountedStringValueTest, OrderingOperators) {
  RefCountedStringValue a("apple");
  RefCountedStringValue b("banana");
  EXPECT_TRUE(a < b);
  EXPECT_FALSE(b < a);
  EXPECT_TRUE(b > a);
  EXPECT_FALSE(a > b);
  EXPECT_TRUE(a < absl::string_view("avocado"));
  EXPECT_TRUE(absl::string_view("aardvark") < a);
  EXPECT_TRUE(b > absl::string_view("apple"));
  EXPECT_TRUE(absl::string_view("cherry") > b);
}

TEST(RefCountedStringValueTest, LessThanFunctorSupportsHeterogeneousLookup) {
  std::map<RefCountedStringValue, int, RefCountedStringValueLessThan> m;
  m.emplace(RefCountedStringValue("one"), 1);
  m.emplace(RefCountedStringValue("two"), 2);
  m.emplace(RefCountedStringValue("three"), 3);
  // Look up by absl::string_view without constructing a RefCountedStringValue.
  auto it = m.find(absl::string_view("two"));
  ASSERT_NE(it, m.end());
  EXPECT_EQ(it->second, 2);
  EXPECT_EQ(m.find(absl::string_view("four")), m.end());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}