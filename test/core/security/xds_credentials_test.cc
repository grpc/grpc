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
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {"a.example.com", "b.example.com"}));
}

TEST(XdsSanMatchingTest, EmptyMatchersList) {
  std::vector<const char*> sans = {"a.example.com", "foo.example.com"};
  EXPECT_TRUE(
      TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(), {}));
}

TEST(XdsSanMatchingTest, IllegalValues) {
  std::vector<const char*> sans = {".a.example.com"};
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {"", "a.example.com", ".a.example.com"}));
  sans = {""};
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(
      sans.data(), sans.size(), {"", "a.example.com", ".a.example.com"}));
}

TEST(XdsSanMatchingTest, ExactMatchDns) {
  std::vector<const char*> sans = {"a.example.com"};
  EXPECT_TRUE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                    {"a.example.com"}));
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"b.example.com"}));
  sans = {"b.example.com."};
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"a.example.com."}));
  EXPECT_TRUE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                    {"b.example.com."}));
}

TEST(XdsSanMatchingTest, ExactMatchWithFullyQualifiedSan) {
  std::vector<const char*> sans = {"a.example.com."};
  EXPECT_TRUE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                    {"a.example.com"}));
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"b.example.com"}));
}

TEST(XdsSanMatchingTest, ExactMatchWithFullyQualifiedMatcher) {
  std::vector<const char*> sans = {"a.example.com"};
  EXPECT_TRUE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                    {"a.example.com."}));
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"b.example.com."}));
}

TEST(XdsSanMatchingTest, ExactMatchDnsCaseInsensitive) {
  std::vector<const char*> sans = {"A.eXaMpLe.CoM"};
  EXPECT_TRUE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                    {"a.example.com"}));
  EXPECT_TRUE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                    {"a.ExAmPlE.cOm"}));
}

TEST(XdsSanMatchingTest, ExactMatchMultipleSansMultipleMatchers) {
  std::vector<const char*> sans = {"a.example.com", "foo.example.com",
                                   "b.example.com"};
  EXPECT_TRUE(TestOnlyVerifySubjectAlternativeNames(
      sans.data(), sans.size(),
      {"abc.example.com", "foo.example.com", "xyz.example.com"}));
}

TEST(XdsSanMatchingTest, WildCardMatch) {
  std::vector<const char*> sans = {"*.example.com"};
  EXPECT_TRUE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                    {"a.example.com"}));
  EXPECT_TRUE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                    {"foo.eXaMplE.cOm"}));
  EXPECT_TRUE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                    {"bar.ExAmPlE.CoM"}));
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {".example.com"}));
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"example.com"}));
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"foo.bar.com"}));
}

TEST(XdsSanMatchingTest, WildCardDoesNotMatchSingleLabelDomain) {
  std::vector<const char*> sans = {"*"};
  EXPECT_FALSE(
      TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(), {"abc"}));
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"abc.com."}));
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"bar.baz.com"}));
  sans = {"*."};
  EXPECT_FALSE(
      TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(), {"abc"}));
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"abc.com."}));
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"bar.baz.com"}));
}

TEST(XdsSanMatchingTest, AsteriskOnlyPermittedInLeftMostDomainName) {
  std::vector<const char*> sans = {"*.example.*.com"};
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"abc.example.xyz.com"}));
  sans = {"*.exam*ple.com"};
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"abc.example.com"}));
}

TEST(XdsSanMatchingTest, AsteriskMustBeOnlyCharacterInLeftMostDomainName) {
  std::vector<const char*> sans = {"*c.example.com"};
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"abc.example.com"}));
}

TEST(XdsSanMatchingTest, AsteriskMatchingAcrossDomainLabelsNotPermitted) {
  std::vector<const char*> sans = {"*.com"};
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"abc.example.com"}));
  EXPECT_FALSE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                     {"foo.bar.baz.com"}));
  EXPECT_TRUE(TestOnlyVerifySubjectAlternativeNames(sans.data(), sans.size(),
                                                    {"abc.com"}));
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
