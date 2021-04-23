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

#include "src/core/lib/security/authorization/grpc_authorization_engine.h"
#include "test/core/util/evaluate_args_test_util.h"

namespace grpc_core {

class MockMatcher : public AuthorizationMatcher {
 public:
  MOCK_METHOD(bool, Matches, (const EvaluateArgs&), (const));
};

TEST(GrpcAuthorizationEngineTest, AllowEngineWithMatchingPolicy) {
  auto matcher1 = absl::make_unique<::testing::StrictMock<MockMatcher>>();
  auto matcher2 = absl::make_unique<::testing::StrictMock<MockMatcher>>();
  // policy1 does not match. policy2 will match.
  EXPECT_CALL(*matcher1, Matches).Times(1).WillOnce(::testing::Return(false));
  EXPECT_CALL(*matcher2, Matches).Times(1).WillOnce(::testing::Return(true));
  GrpcAuthorizationEngine engine(Rbac::Action::kAllow);
  std::map<std::string, std::unique_ptr<AuthorizationMatcher>> matchers;
  matchers["policy1"] = std::move(matcher1);
  matchers["policy2"] = std::move(matcher2);
  engine.SetPoliciesForTesting(std::move(matchers));
  AuthorizationEngine::Decision decision = engine.Evaluate(EvaluateArgs{});
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kAllow);
  EXPECT_EQ(decision.matching_policy_name, "policy2");
}

TEST(GrpcAuthorizationEngineTest, AllowEngineWithNoMatchingPolicy) {
  auto matcher = absl::make_unique<::testing::StrictMock<MockMatcher>>();
  EXPECT_CALL(*matcher, Matches).Times(1).WillOnce(::testing::Return(false));
  GrpcAuthorizationEngine engine(Rbac::Action::kAllow);
  std::map<std::string, std::unique_ptr<AuthorizationMatcher>> matchers;
  matchers["policy1"] = std::move(matcher);
  engine.SetPoliciesForTesting(std::move(matchers));
  AuthorizationEngine::Decision decision = engine.Evaluate(EvaluateArgs{});
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kDeny);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST(GrpcAuthorizationEngineTest, AllowEngineWithEmptyPolicies) {
  GrpcAuthorizationEngine engine(Rbac::Action::kAllow);
  AuthorizationEngine::Decision decision = engine.Evaluate(EvaluateArgs{});
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kDeny);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST(GrpcAuthorizationEngineTest, DenyEngineWithMatchingPolicy) {
  auto matcher1 = absl::make_unique<::testing::StrictMock<MockMatcher>>();
  auto matcher2 = absl::make_unique<::testing::StrictMock<MockMatcher>>();
  // policy1 does not match. policy2 will match.
  EXPECT_CALL(*matcher1, Matches).Times(1).WillOnce(::testing::Return(false));
  EXPECT_CALL(*matcher2, Matches).Times(1).WillOnce(::testing::Return(true));
  GrpcAuthorizationEngine engine(Rbac::Action::kDeny);
  std::map<std::string, std::unique_ptr<AuthorizationMatcher>> matchers;
  matchers["policy1"] = std::move(matcher1);
  matchers["policy2"] = std::move(matcher2);
  engine.SetPoliciesForTesting(std::move(matchers));
  AuthorizationEngine::Decision decision = engine.Evaluate(EvaluateArgs{});
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kDeny);
  EXPECT_EQ(decision.matching_policy_name, "policy2");
}

TEST(GrpcAuthorizationEngineTest, DenyEngineWithNoMatchingPolicy) {
  auto matcher = absl::make_unique<::testing::StrictMock<MockMatcher>>();
  EXPECT_CALL(*matcher, Matches).Times(1).WillOnce(::testing::Return(false));
  GrpcAuthorizationEngine engine(Rbac::Action::kDeny);
  std::map<std::string, std::unique_ptr<AuthorizationMatcher>> matchers;
  matchers["policy1"] = std::move(matcher);
  engine.SetPoliciesForTesting(std::move(matchers));
  AuthorizationEngine::Decision decision = engine.Evaluate(EvaluateArgs{});
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kAllow);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST(GrpcAuthorizationEngineTest, DenyEngineWithEmptyPolicies) {
  GrpcAuthorizationEngine engine(Rbac::Action::kDeny);
  AuthorizationEngine::Decision decision = engine.Evaluate(EvaluateArgs{});
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kAllow);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST(GrpcAuthorizationEngineTest, VerifiesMatchersCreation) {
  EvaluateArgsTestUtil util;
  util.AddPairToMetadata("foo", "bar");
  Rbac::Policy policy(
      Rbac::Permission(
          Rbac::Permission::RuleType::kHeader,
          HeaderMatcher::Create("foo", HeaderMatcher::Type::kExact, "bar")
              .value()),
      Rbac::Principal(Rbac::Principal::RuleType::kAny));
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy);
  Rbac rbac(Rbac::Action::kAllow, std::move(policies));
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(util.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kAllow);
  EXPECT_EQ(decision.matching_policy_name, "policy1");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
