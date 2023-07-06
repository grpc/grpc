// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/match.h"

#include "gtest/gtest.h"

namespace {

TEST(MatchTest, StartsWith) {
  const std::string s1("123\0abc", 7);
  const absl::string_view a("foobar");
  const absl::string_view b(s1);
  const absl::string_view e;
  EXPECT_TRUE(absl::StartsWith(a, a));
  EXPECT_TRUE(absl::StartsWith(a, "foo"));
  EXPECT_TRUE(absl::StartsWith(a, e));
  EXPECT_TRUE(absl::StartsWith(b, s1));
  EXPECT_TRUE(absl::StartsWith(b, b));
  EXPECT_TRUE(absl::StartsWith(b, e));
  EXPECT_TRUE(absl::StartsWith(e, ""));
  EXPECT_FALSE(absl::StartsWith(a, b));
  EXPECT_FALSE(absl::StartsWith(b, a));
  EXPECT_FALSE(absl::StartsWith(e, a));
}

TEST(MatchTest, EndsWith) {
  const std::string s1("123\0abc", 7);
  const absl::string_view a("foobar");
  const absl::string_view b(s1);
  const absl::string_view e;
  EXPECT_TRUE(absl::EndsWith(a, a));
  EXPECT_TRUE(absl::EndsWith(a, "bar"));
  EXPECT_TRUE(absl::EndsWith(a, e));
  EXPECT_TRUE(absl::EndsWith(b, s1));
  EXPECT_TRUE(absl::EndsWith(b, b));
  EXPECT_TRUE(absl::EndsWith(b, e));
  EXPECT_TRUE(absl::EndsWith(e, ""));
  EXPECT_FALSE(absl::EndsWith(a, b));
  EXPECT_FALSE(absl::EndsWith(b, a));
  EXPECT_FALSE(absl::EndsWith(e, a));
}

TEST(MatchTest, Contains) {
  absl::string_view a("abcdefg");
  absl::string_view b("abcd");
  absl::string_view c("efg");
  absl::string_view d("gh");
  EXPECT_TRUE(absl::StrContains(a, a));
  EXPECT_TRUE(absl::StrContains(a, b));
  EXPECT_TRUE(absl::StrContains(a, c));
  EXPECT_FALSE(absl::StrContains(a, d));
  EXPECT_TRUE(absl::StrContains("", ""));
  EXPECT_TRUE(absl::StrContains("abc", ""));
  EXPECT_FALSE(absl::StrContains("", "a"));
}

TEST(MatchTest, ContainsChar) {
  absl::string_view a("abcdefg");
  absl::string_view b("abcd");
  EXPECT_TRUE(absl::StrContains(a, 'a'));
  EXPECT_TRUE(absl::StrContains(a, 'b'));
  EXPECT_TRUE(absl::StrContains(a, 'e'));
  EXPECT_FALSE(absl::StrContains(a, 'h'));

  EXPECT_TRUE(absl::StrContains(b, 'a'));
  EXPECT_TRUE(absl::StrContains(b, 'b'));
  EXPECT_FALSE(absl::StrContains(b, 'e'));
  EXPECT_FALSE(absl::StrContains(b, 'h'));

  EXPECT_FALSE(absl::StrContains("", 'a'));
  EXPECT_FALSE(absl::StrContains("", 'a'));
}

TEST(MatchTest, ContainsNull) {
  const std::string s = "foo";
  const char* cs = "foo";
  const absl::string_view sv("foo");
  const absl::string_view sv2("foo\0bar", 4);
  EXPECT_EQ(s, "foo");
  EXPECT_EQ(sv, "foo");
  EXPECT_NE(sv2, "foo");
  EXPECT_TRUE(absl::EndsWith(s, sv));
  EXPECT_TRUE(absl::StartsWith(cs, sv));
  EXPECT_TRUE(absl::StrContains(cs, sv));
  EXPECT_FALSE(absl::StrContains(cs, sv2));
}

TEST(MatchTest, EqualsIgnoreCase) {
  std::string text = "the";
  absl::string_view data(text);

  EXPECT_TRUE(absl::EqualsIgnoreCase(data, "The"));
  EXPECT_TRUE(absl::EqualsIgnoreCase(data, "THE"));
  EXPECT_TRUE(absl::EqualsIgnoreCase(data, "the"));
  EXPECT_FALSE(absl::EqualsIgnoreCase(data, "Quick"));
  EXPECT_FALSE(absl::EqualsIgnoreCase(data, "then"));
}

TEST(MatchTest, StartsWithIgnoreCase) {
  EXPECT_TRUE(absl::StartsWithIgnoreCase("foo", "foo"));
  EXPECT_TRUE(absl::StartsWithIgnoreCase("foo", "Fo"));
  EXPECT_TRUE(absl::StartsWithIgnoreCase("foo", ""));
  EXPECT_FALSE(absl::StartsWithIgnoreCase("foo", "fooo"));
  EXPECT_FALSE(absl::StartsWithIgnoreCase("", "fo"));
}

TEST(MatchTest, EndsWithIgnoreCase) {
  EXPECT_TRUE(absl::EndsWithIgnoreCase("foo", "foo"));
  EXPECT_TRUE(absl::EndsWithIgnoreCase("foo", "Oo"));
  EXPECT_TRUE(absl::EndsWithIgnoreCase("foo", ""));
  EXPECT_FALSE(absl::EndsWithIgnoreCase("foo", "fooo"));
  EXPECT_FALSE(absl::EndsWithIgnoreCase("", "fo"));
}

}  // namespace
