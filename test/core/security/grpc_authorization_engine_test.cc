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

namespace grpc_core {

constexpr const char* kPolicy1 = "policy1";
constexpr const char* kPolicy2 = "policy2";

class MockMatcher : public AuthorizationMatcher {
 public:
  MOCK_METHOD(bool, Matches, (const EvaluateArgs&), (const));
};

class GrpcAuthorizationEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    matcher1_ = absl::make_unique<::testing::StrictMock<MockMatcher>>();
    matcher2_ = absl::make_unique<::testing::StrictMock<MockMatcher>>();
  }

  GrpcAuthorizationEngine MakeAuthorizationEngine(Rbac::Action action) {
    std::map<std::string, std::unique_ptr<AuthorizationMatcher>> matchers;
    matchers[kPolicy1] = std::move(matcher1_);
    matchers[kPolicy2] = std::move(matcher2_);
    return GrpcAuthorizationEngine(action, std::move(matchers));
  }

  GrpcAuthorizationEngine MakeEmptyAuthorizationEngine(Rbac::Action action) {
    return GrpcAuthorizationEngine(action);
  }

  std::unique_ptr<::testing::StrictMock<MockMatcher>> matcher1_;
  std::unique_ptr<::testing::StrictMock<MockMatcher>> matcher2_;
};

TEST_F(GrpcAuthorizationEngineTest, AllowEngineWithMatchingPolicy) {
  // Policy1 does not match. Policy2 will match.
  EXPECT_CALL(*matcher1_, Matches).Times(1).WillOnce(::testing::Return(false));
  EXPECT_CALL(*matcher2_, Matches).Times(1).WillOnce(::testing::Return(true));
  auto engine = MakeAuthorizationEngine(Rbac::Action::kAllow);
  AuthorizationEngine::Decision decision = engine.Evaluate(EvaluateArgs{});
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kAllow);
  EXPECT_EQ(decision.matching_policy_name, kPolicy2);
}

TEST_F(GrpcAuthorizationEngineTest, AllowEngineWithNoMatchingPolicy) {
  // Policy1 and Policy2 do not match.
  EXPECT_CALL(*matcher1_, Matches).Times(1).WillOnce(::testing::Return(false));
  EXPECT_CALL(*matcher2_, Matches).Times(1).WillOnce(::testing::Return(false));
  auto engine = MakeAuthorizationEngine(Rbac::Action::kAllow);
  AuthorizationEngine::Decision decision = engine.Evaluate(EvaluateArgs{});
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kDeny);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST_F(GrpcAuthorizationEngineTest, AllowEngineWithEmptyPolicies) {
  auto engine = MakeEmptyAuthorizationEngine(Rbac::Action::kAllow);
  AuthorizationEngine::Decision decision = engine.Evaluate(EvaluateArgs{});
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kDeny);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST_F(GrpcAuthorizationEngineTest, DenyEngineWithMatchingPolicy) {
  // Policy1 does not match. Policy2 will match.
  EXPECT_CALL(*matcher1_, Matches).Times(1).WillOnce(::testing::Return(false));
  EXPECT_CALL(*matcher2_, Matches).Times(1).WillOnce(::testing::Return(true));
  auto engine = MakeAuthorizationEngine(Rbac::Action::kDeny);
  AuthorizationEngine::Decision decision = engine.Evaluate(EvaluateArgs{});
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kDeny);
  EXPECT_EQ(decision.matching_policy_name, kPolicy2);
}

TEST_F(GrpcAuthorizationEngineTest, DenyEngineWithNoMatchingPolicy) {
  // Policy1 and Policy2 do not match.
  EXPECT_CALL(*matcher1_, Matches).Times(1).WillOnce(::testing::Return(false));
  EXPECT_CALL(*matcher2_, Matches).Times(1).WillOnce(::testing::Return(false));
  auto engine = MakeAuthorizationEngine(Rbac::Action::kDeny);
  AuthorizationEngine::Decision decision = engine.Evaluate(EvaluateArgs{});
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kAllow);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST_F(GrpcAuthorizationEngineTest, DenyEngineWithEmptyPolicies) {
  auto engine = MakeEmptyAuthorizationEngine(Rbac::Action::kDeny);
  AuthorizationEngine::Decision decision = engine.Evaluate(EvaluateArgs{});
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::DecisionType::kAllow);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
