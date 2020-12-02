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

TEST(XdsSanMatchingTest, EmptySansList) {
  std::vector<const char*> sans = {};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "a.example.com"),
       XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "b.example.com")}));
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
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             ""),
       XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "a.example.com"),
       XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             ".a.example.com")}));
  sans = {""};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             ""),
       XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "a.example.com"),
       XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             ".a.example.com")}));
  sans = {"a.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             ""),
       XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "a.example.com"),
       XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             ".a.example.com")}));
}

TEST(XdsSanMatchingTest, ExactMatchDns) {
  std::vector<const char*> sans = {"a.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "a.example.com")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "b.example.com")}));
  sans = {"b.example.com."};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "a.example.com.")}));
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "b.example.com.")}));
}

TEST(XdsSanMatchingTest, ExactMatchWithFullyQualifiedSan) {
  std::vector<const char*> sans = {"a.example.com."};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "a.example.com")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "b.example.com")}));
}

TEST(XdsSanMatchingTest, ExactMatchWithFullyQualifiedMatcher) {
  std::vector<const char*> sans = {"a.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "a.example.com.")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "b.example.com.")}));
}

TEST(XdsSanMatchingTest, ExactMatchDnsCaseInsensitive) {
  std::vector<const char*> sans = {"A.eXaMpLe.CoM"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "a.example.com")}));
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "a.ExAmPlE.cOm")}));
}

TEST(XdsSanMatchingTest, ExactMatchMultipleSansMultipleMatchers) {
  std::vector<const char*> sans = {"a.example.com", "foo.example.com",
                                   "b.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "abc.example.com"),
       XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "foo.example.com"),
       XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "xyz.example.com")}));
}

TEST(XdsSanMatchingTest, ExactMatchWildCard) {
  std::vector<const char*> sans = {"*.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "a.example.com")}));
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "fOo.ExAmPlE.cOm")}));
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "BaR.eXaMpLe.CoM")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             ".example.com")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "example.com")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "foo.bar.com")}));
}

TEST(XdsSanMatchingTest, ExactMatchWildCardDoesNotMatchSingleLabelDomain) {
  std::vector<const char*> sans = {"*"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "abc")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "abc.com.")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "bar.baz.com")}));
  sans = {"*."};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "abc")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "abc.com.")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "bar.baz.com")}));
}

TEST(XdsSanMatchingTest, ExactMatchAsteriskOnlyPermittedInLeftMostDomainName) {
  std::vector<const char*> sans = {"*.example.*.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "abc.example.xyz.com")}));
  sans = {"*.exam*ple.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "abc.example.com")}));
}

TEST(XdsSanMatchingTest,
     ExactMatchAsteriskMustBeOnlyCharacterInLeftMostDomainName) {
  std::vector<const char*> sans = {"*c.example.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "abc.example.com")}));
}

TEST(XdsSanMatchingTest,
     ExactMatchAsteriskMatchingAcrossDomainLabelsNotPermitted) {
  std::vector<const char*> sans = {"*.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "abc.example.com")}));
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "foo.bar.baz.com")}));
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::EXACT,
                             "abc.com")}));
}

TEST(XdsSanMatchingTest, PrefixMatch) {
  std::vector<const char*> sans = {"abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::PREFIX,
                             "abc")}));
  sans = {"AbC.CoM"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::PREFIX,
                             "abc")}));
  sans = {"xyz.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::PREFIX,
                             "abc")}));
}

TEST(XdsSanMatchingTest, PrefixMatchIgnoreCase) {
  std::vector<const char*> sans = {"aBc.cOm"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::PREFIX,
                             "AbC", true /* ignore_case */)}));
  sans = {"abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::PREFIX,
                             "AbC", true /* ignore_case */)}));
  sans = {"xyz.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::PREFIX,
                             "AbC", true /* ignore_case */)}));
}

TEST(XdsSanMatchingTest, SuffixMatch) {
  std::vector<const char*> sans = {"abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::SUFFIX,
                             ".com")}));
  sans = {"AbC.CoM"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::SUFFIX,
                             ".com")}));
  sans = {"abc.xyz"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::SUFFIX,
                             ".com")}));
}

TEST(XdsSanMatchingTest, SuffixMatchIgnoreCase) {
  std::vector<const char*> sans = {"abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::SUFFIX,
                             ".CoM", true /* ignore_case */)}));
  sans = {"AbC.cOm"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::SUFFIX,
                             ".CoM", true /* ignore_case */)}));
  sans = {"abc.xyz"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::SUFFIX,
                             ".CoM", true /* ignore_case */)}));
}

TEST(XdsSanMatchingTest, ContainsMatch) {
  std::vector<const char*> sans = {"abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::CONTAINS,
                             "abc")}));
  sans = {"xyz.abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::CONTAINS,
                             "abc")}));
  sans = {"foo.AbC.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::CONTAINS,
                             "abc")}));
}

TEST(XdsSanMatchingTest, ContainsMatchIgnoresCase) {
  std::vector<const char*> sans = {"abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::CONTAINS,
                             "AbC", true /* ignore_case */)}));
  sans = {"xyz.abc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::CONTAINS,
                             "AbC", true /* ignore_case */)}));
  sans = {"foo.aBc.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::CONTAINS,
                             "AbC", true /* ignore_case */)}));
  sans = {"foo.Ab.com"};
  EXPECT_FALSE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(XdsApi::StringMatcher::StringMatcherType::CONTAINS,
                             "AbC", true /* ignore_case */)}));
}

TEST(XdsSanMatchingTest, RegexMatch) {
  std::vector<const char*> sans = {"abc.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(
          XdsApi::StringMatcher::StringMatcherType::SAFE_REGEX,
          "(abc|xyz).example.com")}));
  sans = {"xyz.example.com"};
  EXPECT_TRUE(TestOnlyXdsVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {XdsApi::StringMatcher(
          XdsApi::StringMatcher::StringMatcherType::SAFE_REGEX,
          "(abc|xyz).example.com")}));
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
