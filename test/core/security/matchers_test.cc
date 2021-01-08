// Copyright 2020 gRPC authors.
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

#include "src/core/lib/security/authorization/matchers.h"

#include <gtest/gtest.h>

namespace grpc_core {

TEST(StringMatcherTest, ExactMatchCaseSensitive) {
  StringMatcher string_matcher(StringMatcher::Type::EXACT,
                               /*matcher=*/"exact", /*case_sensitive=*/true);
  EXPECT_TRUE(string_matcher.Match("exact"));
  EXPECT_FALSE(string_matcher.Match("Exact"));
  EXPECT_FALSE(string_matcher.Match("exacz"));
}

TEST(StringMatcherTest, ExactMatchCaseInsensitive) {
  StringMatcher string_matcher(StringMatcher::Type::EXACT,
                               /*matcher=*/"exact", /*case_sensitive=*/false);
  EXPECT_TRUE(string_matcher.Match("Exact"));
  EXPECT_FALSE(string_matcher.Match("Exacz"));
}

TEST(StringMatcherTest, PrefixMatchCaseSensitive) {
  StringMatcher string_matcher(StringMatcher::Type::PREFIX,
                               /*matcher=*/"prefix",
                               /*case_sensitive=*/true);
  EXPECT_TRUE(string_matcher.Match("prefix-test"));
  EXPECT_FALSE(string_matcher.Match("xx-prefix-test"));
  EXPECT_FALSE(string_matcher.Match("Prefix-test"));
  EXPECT_FALSE(string_matcher.Match("pre-test"));
}

TEST(StringMatcherTest, PrefixMatchCaseInsensitive) {
  StringMatcher string_matcher(StringMatcher::Type::PREFIX,
                               /*matcher=*/"prefix",
                               /*case_sensitive=*/false);
  EXPECT_TRUE(string_matcher.Match("PREfix-test"));
  EXPECT_FALSE(string_matcher.Match("xx-PREfix-test"));
  EXPECT_FALSE(string_matcher.Match("PRE-test"));
}

TEST(StringMatcherTest, SuffixMatchCaseSensitive) {
  StringMatcher string_matcher(StringMatcher::Type::SUFFIX,
                               /*matcher=*/"suffix",
                               /*case_sensitive=*/true);
  EXPECT_TRUE(string_matcher.Match("test-suffix"));
  EXPECT_FALSE(string_matcher.Match("test-Suffix"));
  EXPECT_FALSE(string_matcher.Match("test-suffix-xx"));
  EXPECT_FALSE(string_matcher.Match("test-suffiz"));
}

TEST(StringMatcherTest, SuffixMatchCaseInSensitive) {
  StringMatcher string_matcher(StringMatcher::Type::SUFFIX,
                               /*matcher=*/"suffix",
                               /*case_sensitive=*/false);
  EXPECT_TRUE(string_matcher.Match("Test-SUFFIX"));
  EXPECT_FALSE(string_matcher.Match("Test-SUFFIX-xx"));
  EXPECT_FALSE(string_matcher.Match("Test-SUFFIZ"));
}

TEST(StringMatcherTest, SafeRegexMatchCaseSensitive) {
  StringMatcher string_matcher(StringMatcher::Type::SAFE_REGEX,
                               /*matcher=*/"regex.*",
                               /*case_sensitive=*/true);
  EXPECT_TRUE(string_matcher.Match("regex-test"));
  EXPECT_FALSE(string_matcher.Match("xx-regex-test"));
  EXPECT_FALSE(string_matcher.Match("Regex-test"));
  EXPECT_FALSE(string_matcher.Match("test-regex"));
}

TEST(StringMatcherTest, SafeRegexMatchCaseInSensitive) {
  StringMatcher string_matcher(StringMatcher::Type::SAFE_REGEX,
                               /*matcher=*/"regex.*",
                               /*case_sensitive=*/false);
  EXPECT_TRUE(string_matcher.Match("regex-test"));
  EXPECT_TRUE(string_matcher.Match("Regex-test"));
  EXPECT_FALSE(string_matcher.Match("xx-Regex-test"));
  EXPECT_FALSE(string_matcher.Match("test-regex"));
}

TEST(StringMatcherTest, ContainsMatchCaseSensitive) {
  StringMatcher string_matcher(StringMatcher::Type::CONTAINS,
                               /*matcher=*/"contains",
                               /*case_sensitive=*/true);
  EXPECT_TRUE(string_matcher.Match("test-contains"));
  EXPECT_TRUE(string_matcher.Match("test-contains-test"));
  EXPECT_FALSE(string_matcher.Match("test-Contains"));
  EXPECT_FALSE(string_matcher.Match("test-containz"));
}

TEST(StringMatcherTest, ContainsMatchCaseInSensitive) {
  StringMatcher string_matcher(StringMatcher::Type::CONTAINS,
                               /*matcher=*/"contains",
                               /*case_sensitive=*/false);
  EXPECT_TRUE(string_matcher.Match("Test-Contains"));
  EXPECT_TRUE(string_matcher.Match("Test-Contains-Test"));
  EXPECT_FALSE(string_matcher.Match("Test-Containz"));
}

TEST(HeaderMatcherTest, StringMatcher) {
  HeaderMatcher header_matcher(/*name=*/"key", HeaderMatcher::Type::EXACT,
                               /*matcher=*/"exact");
  EXPECT_TRUE(header_matcher.Match("exact"));
  EXPECT_FALSE(header_matcher.Match("Exact"));
  EXPECT_FALSE(header_matcher.Match("exacz"));
}

TEST(HeaderMatcherTest, StringMatcherWithInvertMatch) {
  HeaderMatcher header_matcher(/*name=*/"key", HeaderMatcher::Type::EXACT,
                               /*matcher=*/"exact",
                               /*range_start=*/0, /*range_end=*/0,
                               /*present_match=*/false, /*invert_match=*/true);
  EXPECT_FALSE(header_matcher.Match("exact"));
  EXPECT_TRUE(header_matcher.Match("Exact"));
  EXPECT_TRUE(header_matcher.Match("exacz"));
}

TEST(HeaderMatcherTest, RangeMatcherValidRange) {
  HeaderMatcher header_matcher(/*name=*/"key", HeaderMatcher::Type::RANGE,
                               /*matcher=*/"", /*range_start=*/10,
                               /*range_end*/ 20);
  EXPECT_TRUE(header_matcher.Match("16"));
  EXPECT_TRUE(header_matcher.Match("10"));
  EXPECT_FALSE(header_matcher.Match("3"));
  EXPECT_FALSE(header_matcher.Match("20"));
}

TEST(HeaderMatcherTest, RangeMatcherInvalidRange) {
  HeaderMatcher header_matcher(/*name=*/"key", HeaderMatcher::Type::RANGE,
                               /*matcher=*/"", /*range_start=*/20,
                               /*range_end*/ 10);
  EXPECT_FALSE(header_matcher.Match("16"));
}

TEST(HeaderMatcherTest, PresentMatcherTrue) {
  HeaderMatcher header_matcher(/*name=*/"key", HeaderMatcher::Type::PRESENT,
                               /*matcher=*/"", /*range_start=*/0,
                               /*range_end=*/0, /*present_match=*/true);
  EXPECT_TRUE(header_matcher.Match("any_value"));
  EXPECT_FALSE(header_matcher.Match(absl::nullopt));
}

TEST(HeaderMatcherTest, PresentMatcherFalse) {
  HeaderMatcher header_matcher(/*name=*/"key", HeaderMatcher::Type::PRESENT,
                               /*matcher=*/"", /*range_start=*/0,
                               /*range_end=*/0, /*present_match=*/false);
  EXPECT_FALSE(header_matcher.Match("any_value"));
  EXPECT_TRUE(header_matcher.Match(absl::nullopt));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
