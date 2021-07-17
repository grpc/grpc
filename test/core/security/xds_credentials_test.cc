//
//
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
//
//

#include "src/core/lib/security/credentials/xds/xds_credentials.h"

#include <gtest/gtest.h>

#include <grpc/grpc.h>

#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

namespace {

StringMatcher ExactMatcher(const char* string) {
  return StringMatcher::Create(StringMatcher::Type::kExact, string).value();
}

StringMatcher PrefixMatcher(const char* string, bool case_sensitive = true) {
  return StringMatcher::Create(StringMatcher::Type::kPrefix, string,
                               case_sensitive)
      .value();
}

StringMatcher SuffixMatcher(const char* string, bool case_sensitive = true) {
  return StringMatcher::Create(StringMatcher::Type::kSuffix, string,
                               case_sensitive)
      .value();
}

StringMatcher ContainsMatcher(const char* string, bool case_sensitive = true) {
  return StringMatcher::Create(StringMatcher::Type::kContains, string,
                               case_sensitive)
      .value();
}

StringMatcher SafeRegexMatcher(const char* string) {
  return StringMatcher::Create(StringMatcher::Type::kSafeRegex, string).value();
}

TEST(XdsSanMatchingTest, EmptySansList) {
  std::vector<const char*> sans = {};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {ExactMatcher("a.example.com"), ExactMatcher("b.example.com")}));
}

TEST(XdsSanMatchingTest, EmptyMatchersList) {
  std::vector<const char*> sans = {"a.example.com", "foo.example.com"};
  EXPECT_TRUE(
      TestOnlyXdsVerifySubjectAlternativeNames(sans.data(), sans.size(), {}));
}

TEST(XdsSanMatchingTest, ExactMatchIllegalValues) {
  std::vector<const char*> sans = {".a.example.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {ExactMatcher(""), ExactMatcher("a.example.com"),
       ExactMatcher(".a.example.com")}));
  sans = {""};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {ExactMatcher(""), ExactMatcher("a.example.com"),
       ExactMatcher(".a.example.com")}));
  sans = {"a.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {ExactMatcher(""), ExactMatcher("a.example.com"),
       ExactMatcher(".a.example.com")}));
}

TEST(XdsSanMatchingTest, ExactMatchDns) {
  std::vector<const char*> sans = {"a.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("a.example.com")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("b.example.com")}));
  sans = {"b.example.com."};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("a.example.com.")}));
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("b.example.com.")}));
}

TEST(XdsSanMatchingTest, ExactMatchWithFullyQualifiedSan) {
  std::vector<const char*> sans = {"a.example.com."};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("a.example.com")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("b.example.com")}));
}

TEST(XdsSanMatchingTest, ExactMatchWithFullyQualifiedMatcher) {
  std::vector<const char*> sans = {"a.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("a.example.com.")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("b.example.com.")}));
}

TEST(XdsSanMatchingTest, ExactMatchDnsCaseInsensitive) {
  std::vector<const char*> sans = {"A.eXaMpLe.CoM"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("a.example.com")}));
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("a.ExAmPlE.cOm")}));
}

TEST(XdsSanMatchingTest, ExactMatchMultipleSansMultipleMatchers) {
  std::vector<const char*> sans = {"a.example.com", "foo.example.com",
                                   "b.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {ExactMatcher("abc.example.com"), ExactMatcher("foo.example.com"),
       ExactMatcher("xyz.example.com")}));
}

TEST(XdsSanMatchingTest, ExactMatchWildCard) {
  std::vector<const char*> sans = {"*.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("a.example.com")}));
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("fOo.ExAmPlE.cOm")}));
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("BaR.eXaMpLe.CoM")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher(".example.com")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("example.com")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("foo.bar.com")}));
}

TEST(XdsSanMatchingTest, ExactMatchWildCardDoesNotMatchSingleLabelDomain) {
  std::vector<const char*> sans = {"*"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("abc")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("abc.com.")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("bar.baz.com")}));
  sans = {"*."};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("abc")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("abc.com.")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("bar.baz.com")}));
}

TEST(XdsSanMatchingTest, ExactMatchAsteriskOnlyPermittedInLeftMostDomainName) {
  std::vector<const char*> sans = {"*.example.*.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("abc.example.xyz.com")}));
  sans = {"*.exam*ple.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("abc.example.com")}));
}

TEST(XdsSanMatchingTest,
     ExactMatchAsteriskMustBeOnlyCharacterInLeftMostDomainName) {
  std::vector<const char*> sans = {"*c.example.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("abc.example.com")}));
}

TEST(XdsSanMatchingTest,
     ExactMatchAsteriskMatchingAcrossDomainLabelsNotPermitted) {
  std::vector<const char*> sans = {"*.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("abc.example.com")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("foo.bar.baz.com")}));
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ExactMatcher("abc.com")}));
}

TEST(XdsSanMatchingTest, PrefixMatch) {
  std::vector<const char*> sans = {"abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                       {PrefixMatcher("abc")}));
  sans = {"AbC.CoM"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {PrefixMatcher("abc")}));
  sans = {"xyz.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {PrefixMatcher("abc")}));
}

TEST(XdsSanMatchingTest, PrefixMatchIgnoreCase) {
  std::vector<const char*> sans = {"aBc.cOm"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {PrefixMatcher("AbC", false /* case_sensitive */)}));
  sans = {"abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {PrefixMatcher("AbC", false /* case_sensitive */)}));
  sans = {"xyz.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {PrefixMatcher("AbC", false /* case_sensitive */)}));
}

TEST(XdsSanMatchingTest, SuffixMatch) {
  std::vector<const char*> sans = {"abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {SuffixMatcher(".com")}));
  sans = {"AbC.CoM"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {SuffixMatcher(".com")}));
  sans = {"abc.xyz"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {SuffixMatcher(".com")}));
}

TEST(XdsSanMatchingTest, SuffixMatchIgnoreCase) {
  std::vector<const char*> sans = {"abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {SuffixMatcher(".CoM", false /* case_sensitive */)}));
  sans = {"AbC.cOm"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {SuffixMatcher(".CoM", false /* case_sensitive */)}));
  sans = {"abc.xyz"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {SuffixMatcher(".CoM", false /* case_sensitive */)}));
}

TEST(XdsSanMatchingTest, ContainsMatch) {
  std::vector<const char*> sans = {"abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ContainsMatcher("abc")}));
  sans = {"xyz.abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ContainsMatcher("abc")}));
  sans = {"foo.AbC.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {ContainsMatcher("abc")}));
}

TEST(XdsSanMatchingTest, ContainsMatchIgnoresCase) {
  std::vector<const char*> sans = {"abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {ContainsMatcher("AbC", false /* case_sensitive */)}));
  sans = {"xyz.abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {ContainsMatcher("AbC", false /* case_sensitive */)}));
  sans = {"foo.aBc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {ContainsMatcher("AbC", false /* case_sensitive */)}));
  sans = {"foo.Ab.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {ContainsMatcher("AbC", false /* case_sensitive */)}));
}

TEST(XdsSanMatchingTest, RegexMatch) {
  std::vector<const char*> sans = {"abc.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {SafeRegexMatcher("(abc|xyz).example.com")}));
  sans = {"xyz.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {SafeRegexMatcher("(abc|xyz).example.com")}));
  sans = {"foo.example.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {SafeRegexMatcher("(abc|xyz).example.com")}));
}

}  // namespace

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
