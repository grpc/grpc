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

#include "src/core/lib/security/authorization/grpc_authorization_engine.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace grpc_core {

TEST(GrpcAuthorizationEngineTest, AllowEngineWithMatchingPolicy) {
  Rbac::Policy policy1(
      Rbac::Permission::MakeNotPermission(
          Rbac::Permission::MakeAnyPermission()),
      Rbac::Principal::MakeNotPrincipal(Rbac::Principal::MakeAnyPrincipal()));
  Rbac::Policy policy2(Rbac::Permission::MakeAnyPermission(),
                       Rbac::Principal::MakeAnyPrincipal());
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  policies["policy2"] = std::move(policy2);
  Rbac rbac("authz", Rbac::Action::kAllow, std::move(policies));
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(EvaluateArgs(nullptr, nullptr));
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kAllow);
  EXPECT_EQ(decision.matching_policy_name, "policy2");
}

TEST(GrpcAuthorizationEngineTest, AllowEngineWithNoMatchingPolicy) {
  Rbac::Policy policy1(
      Rbac::Permission::MakeNotPermission(
          Rbac::Permission::MakeAnyPermission()),
      Rbac::Principal::MakeNotPrincipal(Rbac::Principal::MakeAnyPrincipal()));
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  Rbac rbac("authz", Rbac::Action::kAllow, std::move(policies));
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(EvaluateArgs(nullptr, nullptr));
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST(GrpcAuthorizationEngineTest, AllowEngineWithEmptyPolicies) {
  GrpcAuthorizationEngine engine(Rbac::Action::kAllow);
  AuthorizationEngine::Decision decision =
      engine.Evaluate(EvaluateArgs(nullptr, nullptr));
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST(GrpcAuthorizationEngineTest, DenyEngineWithMatchingPolicy) {
  Rbac::Policy policy1(
      Rbac::Permission::MakeNotPermission(
          Rbac::Permission::MakeAnyPermission()),
      Rbac::Principal::MakeNotPrincipal(Rbac::Principal::MakeAnyPrincipal()));
  Rbac::Policy policy2(Rbac::Permission::MakeAnyPermission(),
                       Rbac::Principal::MakeAnyPrincipal());
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  policies["policy2"] = std::move(policy2);
  Rbac rbac("authz", Rbac::Action::kDeny, std::move(policies));
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(EvaluateArgs(nullptr, nullptr));
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_EQ(decision.matching_policy_name, "policy2");
}

TEST(GrpcAuthorizationEngineTest, DenyEngineWithNoMatchingPolicy) {
  Rbac::Policy policy1(
      Rbac::Permission::MakeNotPermission(
          Rbac::Permission::MakeAnyPermission()),
      Rbac::Principal::MakeNotPrincipal(Rbac::Principal::MakeAnyPrincipal()));
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  Rbac rbac("authz", Rbac::Action::kDeny, std::move(policies));
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(EvaluateArgs(nullptr, nullptr));
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kAllow);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST(GrpcAuthorizationEngineTest, DenyEngineWithEmptyPolicies) {
  GrpcAuthorizationEngine engine(Rbac::Action::kDeny);
  AuthorizationEngine::Decision decision =
      engine.Evaluate(EvaluateArgs(nullptr, nullptr));
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kAllow);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
