// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <list>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/security/authorization/evaluate_args.h"
#include "src/core/lib/security/authorization/matchers.h"
#include "test/core/util/evaluate_args_test_util.h"

namespace grpc_core {

class AuthorizationMatchersTest : public ::testing::Test {
 protected:
  EvaluateArgsTestUtil args_;
};

TEST_F(AuthorizationMatchersTest, AlwaysAuthorizationMatcher) {
  EvaluateArgs args = args_.MakeEvaluateArgs();
  AlwaysAuthorizationMatcher matcher;
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotAlwaysAuthorizationMatcher) {
  EvaluateArgs args = args_.MakeEvaluateArgs();
  AlwaysAuthorizationMatcher matcher(/*not_rule=*/true);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, AndAuthorizationMatcherSuccessfulMatch) {
  args_.AddPairToMetadata("foo", "bar");
  args_.SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kHeader,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::kExact,
                            /*matcher=*/"bar")
          .value()));
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kDestPort, /*port=*/123));
  AndAuthorizationMatcher matcher(std::move(rules));
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, AndAuthorizationMatcherFailedMatch) {
  args_.AddPairToMetadata("foo", "not_bar");
  args_.SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kHeader,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::kExact,
                            /*matcher=*/"bar")
          .value()));
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kDestPort, /*port=*/123));
  AndAuthorizationMatcher matcher(std::move(rules));
  // Header rule fails. Expected value "bar", got "not_bar" for key "foo".
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotAndAuthorizationMatcher) {
  args_.AddPairToMetadata(":path", "/expected/foo");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  StringMatcher string_matcher =
      StringMatcher::Create(StringMatcher::Type::kExact,
                            /*matcher=*/"/expected/foo",
                            /*case_sensitive=*/false)
          .value();
  std::vector<std::unique_ptr<Rbac::Permission>> ids;
  ids.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kPath, std::move(string_matcher)));
  AndAuthorizationMatcher matcher(std::move(ids), /*not_rule=*/true);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, OrAuthorizationMatcherSuccessfulMatch) {
  args_.AddPairToMetadata("foo", "bar");
  args_.SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  HeaderMatcher header_matcher =
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::kExact,
                            /*matcher=*/"bar")
          .value();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kHeader, header_matcher));
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kDestPort, /*port=*/456));
  OrAuthorizationMatcher matcher(std::move(rules));
  // Matches as header rule matches even though port rule fails.
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, OrAuthorizationMatcherFailedMatch) {
  args_.AddPairToMetadata("foo", "not_bar");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kHeader,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::kExact,
                            /*matcher=*/"bar")
          .value()));
  OrAuthorizationMatcher matcher(std::move(rules));
  // Header rule fails. Expected value "bar", got "not_bar" for key "foo".
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotOrAuthorizationMatcher) {
  args_.AddPairToMetadata("foo", "not_bar");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kHeader,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::kExact,
                            /*matcher=*/"bar")
          .value()));
  OrAuthorizationMatcher matcher(std::move(rules), /*not_rule=*/true);
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, HybridAuthorizationMatcherSuccessfulMatch) {
  args_.AddPairToMetadata("foo", "bar");
  args_.SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> sub_and_rules;
  sub_and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kHeader,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::kExact,
                            /*matcher=*/"bar")
          .value()));
  std::vector<std::unique_ptr<Rbac::Permission>> sub_or_rules;
  sub_or_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kDestPort, /*port=*/123));
  std::vector<std::unique_ptr<Rbac::Permission>> and_rules;
  and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kAnd, std::move(sub_and_rules)));
  and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kOr, std::move(std::move(sub_or_rules))));
  AndAuthorizationMatcher matcher(std::move(and_rules));
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, HybridAuthorizationMatcherFailedMatch) {
  args_.AddPairToMetadata("foo", "bar");
  args_.SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> sub_and_rules;
  sub_and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kHeader,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::kExact,
                            /*matcher=*/"bar")
          .value()));
  sub_and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kHeader,
      HeaderMatcher::Create(/*name=*/"absent_key", HeaderMatcher::Type::kExact,
                            /*matcher=*/"some_value")
          .value()));
  std::vector<std::unique_ptr<Rbac::Permission>> sub_or_rules;
  sub_or_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kDestPort, /*port=*/123));
  std::vector<std::unique_ptr<Rbac::Permission>> and_rules;
  and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kAnd, std::move(sub_and_rules)));
  and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kOr, std::move(std::move(sub_or_rules))));
  AndAuthorizationMatcher matcher(std::move(and_rules));
  // Fails as "absent_key" header was not present.
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, PathAuthorizationMatcherSuccessfulMatch) {
  args_.AddPairToMetadata(":path", "expected/path");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  PathAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::kExact,
                            /*matcher=*/"expected/path",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, PathAuthorizationMatcherFailedMatch) {
  args_.AddPairToMetadata(":path", "different/path");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  PathAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::kExact,
                            /*matcher=*/"expected/path",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotPathAuthorizationMatcher) {
  args_.AddPairToMetadata(":path", "expected/path");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  PathAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::kExact, "expected/path", false)
          .value(),
      /*not_rule=*/true);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest,
       PathAuthorizationMatcherFailedMatchMissingPath) {
  EvaluateArgs args = args_.MakeEvaluateArgs();
  PathAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::kExact,
                            /*matcher=*/"expected/path",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, HeaderAuthorizationMatcherSuccessfulMatch) {
  args_.AddPairToMetadata("key123", "foo_xxx");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  HeaderAuthorizationMatcher matcher(
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::kPrefix,
                            /*matcher=*/"foo")
          .value());
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, HeaderAuthorizationMatcherFailedMatch) {
  args_.AddPairToMetadata("key123", "foo");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  HeaderAuthorizationMatcher matcher(
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::kExact,
                            /*matcher=*/"bar")
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest,
       HeaderAuthorizationMatcherFailedMatchMultivaluedHeader) {
  args_.AddPairToMetadata("key123", "foo");
  args_.AddPairToMetadata("key123", "bar");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  HeaderAuthorizationMatcher matcher(
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::kExact,
                            /*matcher=*/"foo")
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest,
       HeaderAuthorizationMatcherFailedMatchMissingHeader) {
  EvaluateArgs args = args_.MakeEvaluateArgs();
  HeaderAuthorizationMatcher matcher(
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::kSuffix,
                            /*matcher=*/"foo")
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotHeaderAuthorizationMatcher) {
  args_.AddPairToMetadata("key123", "foo");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  HeaderAuthorizationMatcher matcher(
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::kExact,
                            /*matcher=*/"bar")
          .value(),
      /*not_rule=*/true);
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, PortAuthorizationMatcherSuccessfulMatch) {
  args_.SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  PortAuthorizationMatcher matcher(/*port=*/123);
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, PortAuthorizationMatcherFailedMatch) {
  args_.SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  PortAuthorizationMatcher matcher(/*port=*/456);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotPortAuthorizationMatcher) {
  args_.SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  PortAuthorizationMatcher matcher(/*port=*/123, /*not_rule=*/true);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest,
       AuthenticatedMatcherUnAuthenticatedConnection) {
  EvaluateArgs args = args_.MakeEvaluateArgs();
  AuthenticatedAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::kExact,
                            /*matcher=*/"foo.com",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest,
       AuthenticatedMatcherAuthenticatedConnectionMatcherUnset) {
  args_.AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                 GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  EvaluateArgs args = args_.MakeEvaluateArgs();
  AuthenticatedAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::kExact,
                            /*matcher=*/"",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest,
       AuthenticatedMatcherSuccessfulSpiffeIdMatches) {
  args_.AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                 GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  args_.AddPropertyToAuthContext(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME,
                                 "spiffe://foo.abc");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  AuthenticatedAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::kExact,
                            /*matcher=*/"spiffe://foo.abc",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, AuthenticatedMatcherFailedSpiffeIdMatches) {
  args_.AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                 GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  args_.AddPropertyToAuthContext(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME,
                                 "spiffe://bar.abc");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  AuthenticatedAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::kExact,
                            /*matcher=*/"spiffe://foo.abc",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, AuthenticatedMatcherFailedNothingMatches) {
  args_.AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                 GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  EvaluateArgs args = args_.MakeEvaluateArgs();
  AuthenticatedAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::kExact,
                            /*matcher=*/"foo",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotAuthenticatedMatcher) {
  args_.AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                                 GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  EvaluateArgs args = args_.MakeEvaluateArgs();
  AuthenticatedAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::kExact, /*matcher=*/"foo",
                            /*case_sensitive=*/false)
          .value(),
      /*not_rule=*/true);
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, PolicyAuthorizationMatcherSuccessfulMatch) {
  args_.AddPairToMetadata("key123", "foo");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kHeader,
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::kExact,
                            /*matcher=*/"foo")
          .value()));
  PolicyAuthorizationMatcher matcher(Rbac::Policy(
      Rbac::Permission(Rbac::Permission::RuleType::kOr, std::move(rules)),
      Rbac::Principal(Rbac::Principal::RuleType::kAny)));
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, PolicyAuthorizationMatcherFailedMatch) {
  args_.AddPairToMetadata("key123", "foo");
  EvaluateArgs args = args_.MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::kHeader,
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::kExact,
                            /*matcher=*/"bar")
          .value()));
  PolicyAuthorizationMatcher matcher(Rbac::Policy(
      Rbac::Permission(Rbac::Permission::RuleType::kOr, std::move(rules)),
      Rbac::Principal(Rbac::Principal::RuleType::kAny)));
  EXPECT_FALSE(matcher.Matches(args));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
