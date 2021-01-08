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

TEST(StringMatcherTest, ExactMatchWithIgnoreCaseFalse) {
  const std::string matcher = "exact";
  const std::string match_value = "exact";
  const std::string unmatch_value1 = "Exact";
  const std::string unmatch_value2 = "exacz";
  StringMatcher string_matcher(StringMatcher::StringMatcherType::EXACT, matcher,
                               /*ignore_case=*/false);
  EXPECT_TRUE(string_matcher.Match(match_value));
  EXPECT_FALSE(string_matcher.Match(unmatch_value1));
  EXPECT_FALSE(string_matcher.Match(unmatch_value2));
}

TEST(StringMatcherTest, ExactMatchWithIgnoreCaseTrue) {
  const std::string matcher = "exact";
  const std::string match_value = "Exact";
  const std::string unmatch_value = "Exacz";
  StringMatcher string_matcher(StringMatcher::StringMatcherType::EXACT, matcher,
                               /*ignore_case=*/true);
  EXPECT_TRUE(string_matcher.Match(match_value));
  EXPECT_FALSE(string_matcher.Match(unmatch_value));
}

TEST(StringMatcherTest, PrefixMatchWithIgnoreCaseFalse) {
  const std::string matcher = "prefix";
  const std::string match_value = "prefix-test";
  const std::string unmatch_value1 = "Prefix-test";
  const std::string unmatch_value2 = "pre-test";
  StringMatcher string_matcher(StringMatcher::StringMatcherType::PREFIX,
                               matcher,
                               /*ignore_case=*/false);
  EXPECT_TRUE(string_matcher.Match(match_value));
  EXPECT_FALSE(string_matcher.Match(unmatch_value1));
  EXPECT_FALSE(string_matcher.Match(unmatch_value2));
}

TEST(StringMatcherTest, PrefixMatchWithIgnoreCaseTrue) {
  const std::string matcher = "prefix";
  const std::string match_value = "PREfix-test";
  const std::string unmatch_value = "PRE-test";
  StringMatcher string_matcher(StringMatcher::StringMatcherType::PREFIX,
                               matcher,
                               /*ignore_case=*/true);
  EXPECT_TRUE(string_matcher.Match(match_value));
  EXPECT_FALSE(string_matcher.Match(unmatch_value));
}

TEST(StringMatcherTest, SuffixMatchWithIgnoreCaseFalse) {
  const std::string matcher = "suffix";
  const std::string match_value = "test-suffix";
  const std::string unmatch_value1 = "test-Suffix";
  const std::string unmatch_value2 = "test-suffiz";
  StringMatcher string_matcher(StringMatcher::StringMatcherType::SUFFIX,
                               matcher,
                               /*ignore_case=*/false);
  EXPECT_TRUE(string_matcher.Match(match_value));
  EXPECT_FALSE(string_matcher.Match(unmatch_value1));
  EXPECT_FALSE(string_matcher.Match(unmatch_value2));
}

TEST(StringMatcherTest, SuffixMatchWithIgnoreCaseTrue) {
  const std::string matcher = "suffix";
  const std::string match_value = "Test-SUFFIX";
  const std::string unmatch_value = "Test-SUFFIZ";
  StringMatcher string_matcher(StringMatcher::StringMatcherType::SUFFIX,
                               matcher,
                               /*ignore_case=*/true);
  EXPECT_TRUE(string_matcher.Match(match_value));
  EXPECT_FALSE(string_matcher.Match(unmatch_value));
}

TEST(StringMatcherTest, SafeRegexMatch) {
  const std::string matcher = "regex.*";
  const std::string match_value = "regex-test";
  const std::string unmatch_value1 = "Regex-test";
  const std::string unmatch_value2 = "test-regex";
  StringMatcher string_matcher(StringMatcher::StringMatcherType::SAFE_REGEX,
                               matcher,
                               /*ignore_case=*/true);
  EXPECT_TRUE(string_matcher.Match(match_value));
  EXPECT_FALSE(string_matcher.Match(unmatch_value1));
  EXPECT_FALSE(string_matcher.Match(unmatch_value2));
}

TEST(StringMatcherTest, SafeRegexMatchWithIgnoreCase) {
  const std::string matcher = "regex.*";
  const std::string match_value = "regex-test";
  const std::string unmatch_value1 = "Regex-test";
  const std::string unmatch_value2 = "test-regex";
  StringMatcher string_matcher(
      StringMatcher::StringMatcherType::SAFE_REGEX, matcher,
      /*ignore_case=*/true, /*use_ignore_case_in_regex=*/true);
  EXPECT_TRUE(string_matcher.Match(match_value));
  EXPECT_TRUE(string_matcher.Match(unmatch_value1));
  EXPECT_FALSE(string_matcher.Match(unmatch_value2));
}

TEST(StringMatcherTest, ContainsMatchWithIgnoreCaseFalse) {
  const std::string matcher = "contains";
  const std::string match_value = "test-contains";
  const std::string unmatch_value1 = "test-Contains";
  const std::string unmatch_value2 = "test-containz";
  StringMatcher string_matcher(StringMatcher::StringMatcherType::CONTAINS,
                               matcher,
                               /*ignore_case=*/false);
  EXPECT_TRUE(string_matcher.Match(match_value));
  EXPECT_FALSE(string_matcher.Match(unmatch_value1));
  EXPECT_FALSE(string_matcher.Match(unmatch_value2));
}

TEST(StringMatcherTest, ContainsMatchWithIgnoreCaseTrue) {
  const std::string matcher = "contains";
  const std::string match_value = "Test-Contains";
  const std::string unmatch_value = "Test-Containz";
  StringMatcher string_matcher(StringMatcher::StringMatcherType::CONTAINS,
                               matcher,
                               /*ignore_case=*/true);
  EXPECT_TRUE(string_matcher.Match(match_value));
  EXPECT_FALSE(string_matcher.Match(unmatch_value));
}

TEST(HeaderMatcherTest, StringMatcher) {
  const std::string name = "key";
  const std::string matcher = "exact";
  const std::string match_value = "exact";
  const std::string unmatch_value1 = "Exact";
  const std::string unmatch_value2 = "exacz";
  HeaderMatcher header_matcher(name, HeaderMatcher::HeaderMatcherType::EXACT,
                               matcher);
  EXPECT_TRUE(header_matcher.Match(match_value));
  EXPECT_FALSE(header_matcher.Match(unmatch_value1));
  EXPECT_FALSE(header_matcher.Match(unmatch_value2));
}

TEST(HeaderMatcherTest, StringMatcherWithInvertMatch) {
  const std::string name = "key";
  const std::string matcher = "exact";
  const std::string match_value = "exact";
  const std::string unmatch_value1 = "Exact";
  const std::string unmatch_value2 = "exacz";
  HeaderMatcher header_matcher(name, HeaderMatcher::HeaderMatcherType::EXACT,
                               matcher, /*range_start=*/0, /*range_end=*/0,
                               /*present_match=*/false, /*invert_match=*/true);
  EXPECT_FALSE(header_matcher.Match(match_value));
  EXPECT_TRUE(header_matcher.Match(unmatch_value1));
  EXPECT_TRUE(header_matcher.Match(unmatch_value2));
}

TEST(HeaderMatcherTest, RangeMatcherValidRange) {
  const std::string name = "key";
  const int64_t range_start = 10;
  const int64_t range_end = 20;
  const std::string match_value = "16";
  const std::string unmatch_value1 = "3";
  const std::string unmatch_value2 = "20";
  HeaderMatcher header_matcher(name, HeaderMatcher::HeaderMatcherType::RANGE,
                               /*matcher=*/"", range_start, range_end);
  EXPECT_TRUE(header_matcher.Match(match_value));
  EXPECT_FALSE(header_matcher.Match(unmatch_value1));
  EXPECT_FALSE(header_matcher.Match(unmatch_value2));
}

TEST(HeaderMatcherTest, RangeMatcherInvalidRange) {
  const std::string name = "key";
  const int64_t range_start = 20;
  const int64_t range_end = 10;
  const std::string match_value = "16";
  HeaderMatcher header_matcher(name, HeaderMatcher::HeaderMatcherType::RANGE,
                               /*matcher=*/"", range_start, range_end);
  EXPECT_FALSE(header_matcher.Match(match_value));
}

TEST(HeaderMatcherTest, PresentMatcherTrue) {
  const std::string name = "key";
  const std::string match_value = "any_value";
  HeaderMatcher header_matcher(name, HeaderMatcher::HeaderMatcherType::PRESENT,
                               /*matcher=*/"", /*range_start=*/0,
                               /*range_end=*/0, /*present_match=*/true);
  EXPECT_TRUE(header_matcher.Match(match_value));
  EXPECT_FALSE(header_matcher.Match(absl::nullopt));
}

TEST(HeaderMatcherTest, PresentMatcherFalse) {
  const std::string name = "key";
  const std::string match_value = "any_value";
  HeaderMatcher header_matcher(name, HeaderMatcher::HeaderMatcherType::PRESENT,
                               /*matcher=*/"", /*range_start=*/0,
                               /*range_end=*/0, /*present_match=*/false);
  EXPECT_TRUE(header_matcher.Match(match_value));
  // Since the config has present_match:false.
  EXPECT_TRUE(header_matcher.Match(absl::nullopt));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
