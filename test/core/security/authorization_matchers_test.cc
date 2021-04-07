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
#include "test/core/util/mock_eval_args_endpoint.h"

namespace grpc_core {

class AuthorizationMatchersTest : public ::testing::Test {
 protected:
  void SetUp() override { grpc_metadata_batch_init(&metadata_); }

  void TearDown() override { grpc_metadata_batch_destroy(&metadata_); }

  void AddPairToMetadata(const char* key, const char* value) {
    metadata_storage_.emplace_back();
    auto& storage = metadata_storage_.back();
    ASSERT_EQ(grpc_metadata_batch_add_tail(
                  &metadata_, &storage,
                  grpc_mdelem_from_slices(
                      grpc_slice_intern(grpc_slice_from_static_string(key)),
                      grpc_slice_intern(grpc_slice_from_static_string(value)))),
              GRPC_ERROR_NONE);
  }

  void SetLocalEndpoint(absl::string_view local_uri) {
    endpoint_.SetLocalAddress(local_uri);
  }

  void SetPeerEndpoint(absl::string_view peer_uri) {
    endpoint_.SetPeer(peer_uri);
  }

  void AddPropertyToAuthContext(const char* name, const char* value) {
    auth_context_.add_cstring_property(name, value);
  }

  EvaluateArgs MakeEvaluateArgs() {
    return EvaluateArgs(&metadata_, &auth_context_, &endpoint_);
  }

  std::list<grpc_linked_mdelem> metadata_storage_;
  grpc_metadata_batch metadata_;
  MockEvalArgsEndpoint endpoint_{/*local_uri=*/"", /*peer_uri=*/""};
  grpc_auth_context auth_context_{nullptr};
};

TEST_F(AuthorizationMatchersTest, AlwaysAuthorizationMatcher) {
  EvaluateArgs args = MakeEvaluateArgs();
  AlwaysAuthorizationMatcher matcher;
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotAlwaysAuthorizationMatcher) {
  EvaluateArgs args = MakeEvaluateArgs();
  AlwaysAuthorizationMatcher matcher(/*not_rule=*/true);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, AndAuthorizationMatcherSuccessfulMatch) {
  AddPairToMetadata("foo", "bar");
  SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value()));
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::DEST_PORT, /*port=*/123));
  AndAuthorizationMatcher matcher(std::move(rules));
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, AndAuthorizationMatcherFailedMatch) {
  AddPairToMetadata("foo", "not_bar");
  SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value()));
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::DEST_PORT, /*port=*/123));
  AndAuthorizationMatcher matcher(std::move(rules));
  // Header rule fails. Expected value "bar", got "not_bar" for key "foo".
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotAndAuthorizationMatcher) {
  AddPairToMetadata(":path", "/expected/foo");
  EvaluateArgs args = MakeEvaluateArgs();
  StringMatcher string_matcher =
      StringMatcher::Create(StringMatcher::Type::EXACT,
                            /*matcher=*/"/expected/foo",
                            /*case_sensitive=*/false)
          .value();
  std::vector<std::unique_ptr<Rbac::Permission>> ids;
  ids.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::PATH, std::move(string_matcher)));
  AndAuthorizationMatcher matcher(std::move(ids), /*not_rule=*/true);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, OrAuthorizationMatcherSuccessfulMatch) {
  AddPairToMetadata("foo", "bar");
  SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = MakeEvaluateArgs();
  HeaderMatcher header_matcher =
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER, header_matcher));
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::DEST_PORT, /*port=*/456));
  OrAuthorizationMatcher matcher(std::move(rules));
  // Matches as header rule matches even though port rule fails.
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, OrAuthorizationMatcherFailedMatch) {
  AddPairToMetadata("foo", "not_bar");
  EvaluateArgs args = MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value()));
  OrAuthorizationMatcher matcher(std::move(rules));
  // Header rule fails. Expected value "bar", got "not_bar" for key "foo".
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotOrAuthorizationMatcher) {
  AddPairToMetadata("foo", "not_bar");
  EvaluateArgs args = MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value()));
  OrAuthorizationMatcher matcher(std::move(rules), /*not_rule=*/true);
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, HybridAuthorizationMatcherSuccessfulMatch) {
  AddPairToMetadata("foo", "bar");
  SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> sub_and_rules;
  sub_and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value()));
  std::vector<std::unique_ptr<Rbac::Permission>> sub_or_rules;
  sub_or_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::DEST_PORT, /*port=*/123));
  std::vector<std::unique_ptr<Rbac::Permission>> and_rules;
  and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::AND, std::move(sub_and_rules)));
  and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::OR, std::move(std::move(sub_or_rules))));
  AndAuthorizationMatcher matcher(std::move(and_rules));
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, HybridAuthorizationMatcherFailedMatch) {
  AddPairToMetadata("foo", "bar");
  SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> sub_and_rules;
  sub_and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"foo", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value()));
  sub_and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"absent_key", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"some_value")
          .value()));
  std::vector<std::unique_ptr<Rbac::Permission>> sub_or_rules;
  sub_or_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::DEST_PORT, /*port=*/123));
  std::vector<std::unique_ptr<Rbac::Permission>> and_rules;
  and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::AND, std::move(sub_and_rules)));
  and_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::OR, std::move(std::move(sub_or_rules))));
  AndAuthorizationMatcher matcher(std::move(and_rules));
  // Fails as "absent_key" header was not present.
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, PathAuthorizationMatcherSuccessfulMatch) {
  AddPairToMetadata(":path", "expected/path");
  EvaluateArgs args = MakeEvaluateArgs();
  PathAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT,
                            /*matcher=*/"expected/path",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, PathAuthorizationMatcherFailedMatch) {
  AddPairToMetadata(":path", "different/path");
  EvaluateArgs args = MakeEvaluateArgs();
  PathAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT,
                            /*matcher=*/"expected/path",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotPathAuthorizationMatcher) {
  AddPairToMetadata(":path", "expected/path");
  EvaluateArgs args = MakeEvaluateArgs();
  PathAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT, "expected/path", false)
          .value(),
      /*not_rule=*/true);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest,
       PathAuthorizationMatcherFailedMatchMissingPath) {
  EvaluateArgs args = MakeEvaluateArgs();
  PathAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT,
                            /*matcher=*/"expected/path",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, HeaderAuthorizationMatcherSuccessfulMatch) {
  AddPairToMetadata("key123", "foo_xxx");
  EvaluateArgs args = MakeEvaluateArgs();
  HeaderAuthorizationMatcher matcher(
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::PREFIX,
                            /*matcher=*/"foo")
          .value());
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, HeaderAuthorizationMatcherFailedMatch) {
  AddPairToMetadata("key123", "foo");
  EvaluateArgs args = MakeEvaluateArgs();
  HeaderAuthorizationMatcher matcher(
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest,
       HeaderAuthorizationMatcherFailedMatchMultivaluedHeader) {
  AddPairToMetadata("key123", "foo");
  AddPairToMetadata("key123", "bar");
  EvaluateArgs args = MakeEvaluateArgs();
  HeaderAuthorizationMatcher matcher(
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"foo")
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest,
       HeaderAuthorizationMatcherFailedMatchMissingHeader) {
  EvaluateArgs args = MakeEvaluateArgs();
  HeaderAuthorizationMatcher matcher(
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::SUFFIX,
                            /*matcher=*/"foo")
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotHeaderAuthorizationMatcher) {
  AddPairToMetadata("key123", "foo");
  EvaluateArgs args = MakeEvaluateArgs();
  HeaderAuthorizationMatcher matcher(
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value(),
      /*not_rule=*/true);
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, PortAuthorizationMatcherSuccessfulMatch) {
  SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = MakeEvaluateArgs();
  PortAuthorizationMatcher matcher(/*port=*/123);
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, PortAuthorizationMatcherFailedMatch) {
  SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = MakeEvaluateArgs();
  PortAuthorizationMatcher matcher(/*port=*/456);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotPortAuthorizationMatcher) {
  SetLocalEndpoint("ipv4:255.255.255.255:123");
  EvaluateArgs args = MakeEvaluateArgs();
  PortAuthorizationMatcher matcher(/*port=*/123, /*not_rule=*/true);
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest,
       AuthenticatedMatcherUnAuthenticatedConnection) {
  EvaluateArgs args = MakeEvaluateArgs();
  AuthenticatedAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT,
                            /*matcher=*/"foo.com",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest,
       AuthenticatedMatcherAuthenticatedConnectionMatcherUnset) {
  AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                           GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  EvaluateArgs args = MakeEvaluateArgs();
  AuthenticatedAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT,
                            /*matcher=*/"",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest,
       AuthenticatedMatcherSuccessfulSpiffeIdMatches) {
  AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                           GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  AddPropertyToAuthContext(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME,
                           "spiffe://foo.abc");
  EvaluateArgs args = MakeEvaluateArgs();
  AuthenticatedAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT,
                            /*matcher=*/"spiffe://foo.abc",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, AuthenticatedMatcherFailedSpiffeIdMatches) {
  AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                           GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  AddPropertyToAuthContext(GRPC_PEER_SPIFFE_ID_PROPERTY_NAME,
                           "spiffe://bar.abc");
  EvaluateArgs args = MakeEvaluateArgs();
  AuthenticatedAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT,
                            /*matcher=*/"spiffe://foo.abc",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, AuthenticatedMatcherFailedNothingMatches) {
  AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                           GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  EvaluateArgs args = MakeEvaluateArgs();
  AuthenticatedAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT,
                            /*matcher=*/"foo",
                            /*case_sensitive=*/false)
          .value());
  EXPECT_FALSE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, NotAuthenticatedMatcher) {
  AddPropertyToAuthContext(GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME,
                           GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  EvaluateArgs args = MakeEvaluateArgs();
  AuthenticatedAuthorizationMatcher matcher(
      StringMatcher::Create(StringMatcher::Type::EXACT, /*matcher=*/"foo",
                            /*case_sensitive=*/false)
          .value(),
      /*not_rule=*/true);
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, PolicyAuthorizationMatcherSuccessfulMatch) {
  AddPairToMetadata("key123", "foo");
  EvaluateArgs args = MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"foo")
          .value()));
  PolicyAuthorizationMatcher matcher(Rbac::Policy(
      Rbac::Permission(Rbac::Permission::RuleType::OR, std::move(rules)),
      Rbac::Principal(Rbac::Principal::RuleType::ANY)));
  EXPECT_TRUE(matcher.Matches(args));
}

TEST_F(AuthorizationMatchersTest, PolicyAuthorizationMatcherFailedMatch) {
  AddPairToMetadata("key123", "foo");
  EvaluateArgs args = MakeEvaluateArgs();
  std::vector<std::unique_ptr<Rbac::Permission>> rules;
  rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"key123", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar")
          .value()));
  PolicyAuthorizationMatcher matcher(Rbac::Policy(
      Rbac::Permission(Rbac::Permission::RuleType::OR, std::move(rules)),
      Rbac::Principal(Rbac::Principal::RuleType::ANY)));
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
