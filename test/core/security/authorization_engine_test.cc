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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/security/authorization/authorization_engine.h"
#include "test/core/util/mock_eval_args_endpoint.h"

namespace grpc_core {

namespace {

void AddPairToMetadataBatch(grpc_metadata_batch* batch,
                            grpc_linked_mdelem* storage, const char* key,
                            const char* value) {
  ASSERT_EQ(grpc_metadata_batch_add_tail(
                batch, storage,
                grpc_mdelem_from_slices(
                    grpc_slice_intern(grpc_slice_from_static_string(key)),
                    grpc_slice_intern(grpc_slice_from_static_string(value)))),
            GRPC_ERROR_NONE);
}

}  // namespace

TEST(AuthorizationEngine, AllowRbacPolicyAllowsRequest) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage1;
  AddPairToMetadataBatch(&metadata, &storage1, /*key=*/"bar_key",
                         /*value=*/"bar_value");
  grpc_linked_mdelem storage2;
  AddPairToMetadataBatch(&metadata, &storage2, /*key=*/":path",
                         /*value=*/"xxx-foo");
  MockEvalArgsEndpoint endpoint(/*local_uri=*/"ipv4:255.255.255.255:80",
                                /*peer_uri=*/"");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, &endpoint);
  Rbac rbac;
  rbac.action = Rbac::Action::ALLOW;
  // Populate "policy1"
  std::vector<std::unique_ptr<Rbac::Permission>> policy1_rules;
  policy1_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::PATH,
      StringMatcher::Create(StringMatcher::Type::EXACT, /*matcher=*/"baz",
                            /*case_sensitive=*/false)
          .value()));
  rbac.policies["policy1"] =
      Rbac::Policy(Rbac::Permission(Rbac::Permission::RuleType::OR,
                                    std::move(policy1_rules)),
                   Rbac::Principal(Rbac::Principal::RuleType::ANY));
  // Populate "policy2"
  std::vector<std::unique_ptr<Rbac::Permission>> policy2_rules;
  policy2_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::PATH,
      StringMatcher::Create(StringMatcher::Type::SUFFIX, /*matcher=*/"foo",
                            /*case_sensitive=*/false)
          .value()));
  policy2_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::DEST_PORT, /*port=*/80));
  std::vector<std::unique_ptr<Rbac::Principal>> policy2_ids;
  policy2_ids.push_back(absl::make_unique<Rbac::Principal>(
      Rbac::Principal::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"bar_key", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar_value")
          .value()));
  rbac.policies["policy2"] = Rbac::Policy(
      Rbac::Permission(Rbac::Permission::RuleType::OR,
                       std::move(policy2_rules)),
      Rbac::Principal(Rbac::Principal::RuleType::OR, std::move(policy2_ids)));
  AuthorizationEngine engine(rbac);
  AuthorizationEngineInterface::AuthorizationDecision decision =
      engine.Evaluate(args);
  EXPECT_EQ(
      decision.type,
      AuthorizationEngineInterface::AuthorizationDecision::DecisionType::ALLOW);
  EXPECT_EQ(decision.matching_policy_name, "policy2");
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationEngine, AllowRbacPolicyDeniesRequest) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/":path",
                         /*value=*/"foo");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr,
                    /*endpoint=*/nullptr);
  Rbac rbac;
  rbac.action = Rbac::Action::ALLOW;
  // Populate "policy1"
  std::vector<std::unique_ptr<Rbac::Permission>> policy1_rules;
  policy1_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::PATH,
      StringMatcher::Create(StringMatcher::Type::EXACT, /*matcher=*/"baz",
                            /*case_sensitive=*/false)
          .value()));
  rbac.policies["policy1"] =
      Rbac::Policy(Rbac::Permission(Rbac::Permission::RuleType::OR,
                                    std::move(policy1_rules)),
                   Rbac::Principal(Rbac::Principal::RuleType::ANY));
  AuthorizationEngine engine(rbac);
  AuthorizationEngineInterface::AuthorizationDecision decision =
      engine.Evaluate(args);
  EXPECT_EQ(
      decision.type,
      AuthorizationEngineInterface::AuthorizationDecision::DecisionType::DENY);
  EXPECT_TRUE(decision.matching_policy_name.empty());
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationEngine, DenyRbacPolicyDeniesRequest) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage1;
  AddPairToMetadataBatch(&metadata, &storage1, /*key=*/"bar_key",
                         /*value=*/"bar_value");
  grpc_linked_mdelem storage2;
  AddPairToMetadataBatch(&metadata, &storage2, /*key=*/":path",
                         /*value=*/"xxx-foo");
  MockEvalArgsEndpoint endpoint(/*local_uri=*/"ipv4:255.255.255.255:80",
                                /*peer_uri=*/"");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr, &endpoint);
  Rbac rbac;
  rbac.action = Rbac::Action::DENY;
  // Populate "policy1"
  std::vector<std::unique_ptr<Rbac::Permission>> policy1_rules;
  policy1_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::PATH,
      StringMatcher::Create(StringMatcher::Type::EXACT, /*matcher=*/"baz",
                            /*case_sensitive=*/false)
          .value()));
  rbac.policies["policy1"] =
      Rbac::Policy(Rbac::Permission(Rbac::Permission::RuleType::OR,
                                    std::move(policy1_rules)),
                   Rbac::Principal(Rbac::Principal::RuleType::ANY));
  // Populate "policy2"
  std::vector<std::unique_ptr<Rbac::Permission>> policy2_rules;
  policy2_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::PATH,
      StringMatcher::Create(StringMatcher::Type::SUFFIX, /*matcher=*/"foo",
                            /*case_sensitive=*/false)
          .value()));
  policy2_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::DEST_PORT, /*port=*/80));
  std::vector<std::unique_ptr<Rbac::Principal>> policy2_ids;
  policy2_ids.push_back(absl::make_unique<Rbac::Principal>(
      Rbac::Principal::RuleType::HEADER,
      HeaderMatcher::Create(/*name=*/"bar_key", HeaderMatcher::Type::EXACT,
                            /*matcher=*/"bar_value")
          .value()));
  rbac.policies["policy2"] = Rbac::Policy(
      Rbac::Permission(Rbac::Permission::RuleType::OR,
                       std::move(policy2_rules)),
      Rbac::Principal(Rbac::Principal::RuleType::OR, std::move(policy2_ids)));
  AuthorizationEngine engine(rbac);
  AuthorizationEngineInterface::AuthorizationDecision decision =
      engine.Evaluate(args);
  EXPECT_EQ(
      decision.type,
      AuthorizationEngineInterface::AuthorizationDecision::DecisionType::DENY);
  EXPECT_EQ(decision.matching_policy_name, "policy2");
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationEngine, DenyRbacPolicyAllowsRequest) {
  grpc_init();
  grpc_metadata_batch metadata;
  grpc_metadata_batch_init(&metadata);
  grpc_linked_mdelem storage;
  AddPairToMetadataBatch(&metadata, &storage, /*key=*/":path",
                         /*value=*/"foo");
  EvaluateArgs args(&metadata, /*auth_context=*/nullptr,
                    /*endpoint=*/nullptr);
  Rbac rbac;
  rbac.action = Rbac::Action::DENY;
  // Populate "policy1"
  std::vector<std::unique_ptr<Rbac::Permission>> policy1_rules;
  policy1_rules.push_back(absl::make_unique<Rbac::Permission>(
      Rbac::Permission::RuleType::PATH,
      StringMatcher::Create(StringMatcher::Type::EXACT, /*matcher=*/"baz",
                            /*case_sensitive=*/false)
          .value()));
  rbac.policies["policy1"] =
      Rbac::Policy(Rbac::Permission(Rbac::Permission::RuleType::OR,
                                    std::move(policy1_rules)),
                   Rbac::Principal(Rbac::Principal::RuleType::ANY));
  AuthorizationEngine engine(rbac);
  AuthorizationEngineInterface::AuthorizationDecision decision =
      engine.Evaluate(args);
  EXPECT_EQ(
      decision.type,
      AuthorizationEngineInterface::AuthorizationDecision::DecisionType::ALLOW);
  EXPECT_TRUE(decision.matching_policy_name.empty());
  grpc_metadata_batch_destroy(&metadata);
  grpc_shutdown();
}

TEST(AuthorizationEngine, AllowsAllRequests) {
  Rbac rbac;
  rbac.action = Rbac::Action::ALLOW;
  rbac.policies["policy123"] =
      Rbac::Policy(Rbac::Permission(Rbac::Permission::RuleType::ANY),
                   Rbac::Principal(Rbac::Principal::RuleType::ANY));
  AuthorizationEngine engine(rbac);
  EvaluateArgs args(/*metadata=*/nullptr, /*auth_context=*/nullptr,
                    /*endpoint=*/nullptr);
  AuthorizationEngineInterface::AuthorizationDecision decision =
      engine.Evaluate(args);
  EXPECT_EQ(
      decision.type,
      AuthorizationEngineInterface::AuthorizationDecision::DecisionType::ALLOW);
  EXPECT_EQ(decision.matching_policy_name, "policy123");
}

TEST(AuthorizationEngine, DenyAllRequests) {
  Rbac rbac;
  rbac.action = Rbac::Action::DENY;
  rbac.policies["policy123"] =
      Rbac::Policy(Rbac::Permission(Rbac::Permission::RuleType::ANY),
                   Rbac::Principal(Rbac::Principal::RuleType::ANY));
  AuthorizationEngine engine(rbac);
  EvaluateArgs args(/*metadata=*/nullptr, /*auth_context=*/nullptr,
                    /*endpoint=*/nullptr);
  AuthorizationEngineInterface::AuthorizationDecision decision =
      engine.Evaluate(args);
  EXPECT_EQ(
      decision.type,
      AuthorizationEngineInterface::AuthorizationDecision::DecisionType::DENY);
  EXPECT_EQ(decision.matching_policy_name, "policy123");
}

TEST(AuthorizationEngine, EmptyPoliciesinAllowRbacPolicy) {
  AuthorizationEngine engine(Rbac::Action::ALLOW);
  EvaluateArgs args(/*metadata=*/nullptr, /*auth_context=*/nullptr,
                    /*endpoint=*/nullptr);
  AuthorizationEngineInterface::AuthorizationDecision decision =
      engine.Evaluate(args);
  EXPECT_EQ(
      decision.type,
      AuthorizationEngineInterface::AuthorizationDecision::DecisionType::DENY);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST(AuthorizationEngine, EmptyPoliciesinDenyRbacPolicy) {
  AuthorizationEngine engine(Rbac::Action::DENY);
  EvaluateArgs args(/*metadata=*/nullptr, /*auth_context=*/nullptr,
                    /*endpoint=*/nullptr);
  AuthorizationEngineInterface::AuthorizationDecision decision =
      engine.Evaluate(args);
  EXPECT_EQ(
      decision.type,
      AuthorizationEngineInterface::AuthorizationDecision::DecisionType::ALLOW);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

class MockMatcher : public Matcher {
 public:
  MOCK_METHOD(bool, Matches, (const EvaluateArgs&), (const));
};

TEST(AuthorizationEngine, AllowsAllRequestsTestTestTest) {
  std::map<std::string, std::unique_ptr<Matcher>> matchers;
  auto matcher1 = absl::make_unique<::testing::StrictMock<MockMatcher>>();
  auto* matcher1_ptr = matcher1.get();
  auto matcher2 = absl::make_unique<::testing::StrictMock<MockMatcher>>();
  auto* matcher2_ptr = matcher2.get();
  EXPECT_CALL(*matcher1_ptr, Matches)
      .Times(1)
      .WillOnce(::testing::Return(false));
  EXPECT_CALL(*matcher2_ptr, Matches)
      .Times(1)
      .WillOnce(::testing::Return(true));
  matchers["policy1"] = std::move(matcher1);
  matchers["policy2"] = std::move(matcher2);
  AuthorizationEngine engine(Rbac::Action::ALLOW, std::move(matchers));
  EvaluateArgs args(/*metadata=*/nullptr, /*auth_context=*/nullptr,
                    /*endpoint=*/nullptr);
  AuthorizationEngineInterface::AuthorizationDecision decision =
      engine.Evaluate(args);
  EXPECT_EQ(
      decision.type,
      AuthorizationEngineInterface::AuthorizationDecision::DecisionType::ALLOW);
  EXPECT_EQ(decision.matching_policy_name, "policy2");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
