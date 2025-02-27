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

#include "src/core/lib/security/authorization/grpc_authorization_engine.h"

#include <grpc/grpc_audit_logging.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/support/port_platform.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/security/authorization/audit_logging.h"
#include "src/core/util/json/json.h"
#include "test/core/test_util/audit_logging_utils.h"
#include "test/core/test_util/evaluate_args_test_util.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kPolicyName = "authz";
constexpr absl::string_view kSpiffeId = "spiffe://foo";
constexpr absl::string_view kRpcMethod = "/foo.Bar/Echo";

using experimental::AuditLoggerRegistry;
using experimental::RegisterAuditLoggerFactory;
using testing::TestAuditLoggerFactory;

class GrpcAuthorizationEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    RegisterAuditLoggerFactory(
        std::make_unique<TestAuditLoggerFactory>(&audit_logs_));
    evaluate_args_util_.AddPairToMetadata(":path", kRpcMethod.data());
    evaluate_args_util_.AddPropertyToAuthContext(
        GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, kSpiffeId.data());
  }

  void TearDown() override { AuditLoggerRegistry::TestOnlyResetRegistry(); }

  std::vector<std::string> audit_logs_;
  EvaluateArgsTestUtil evaluate_args_util_;
};

}  // namespace

TEST_F(GrpcAuthorizationEngineTest, AllowEngineWithMatchingPolicy) {
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

TEST_F(GrpcAuthorizationEngineTest, AllowEngineWithNoMatchingPolicy) {
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

TEST_F(GrpcAuthorizationEngineTest, AllowEngineWithEmptyPolicies) {
  GrpcAuthorizationEngine engine(Rbac::Action::kAllow);
  AuthorizationEngine::Decision decision =
      engine.Evaluate(EvaluateArgs(nullptr, nullptr));
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST_F(GrpcAuthorizationEngineTest, DenyEngineWithMatchingPolicy) {
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

TEST_F(GrpcAuthorizationEngineTest, DenyEngineWithNoMatchingPolicy) {
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

TEST_F(GrpcAuthorizationEngineTest, DenyEngineWithEmptyPolicies) {
  GrpcAuthorizationEngine engine(Rbac::Action::kDeny);
  AuthorizationEngine::Decision decision =
      engine.Evaluate(EvaluateArgs(nullptr, nullptr));
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kAllow);
  EXPECT_TRUE(decision.matching_policy_name.empty());
}

TEST_F(GrpcAuthorizationEngineTest, AuditLoggerNoneNotInvokedOnAllowedRequest) {
  Rbac::Policy policy1(Rbac::Permission::MakeAnyPermission(),
                       Rbac::Principal::MakeAnyPrincipal());
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  Rbac rbac(std::string(kPolicyName), Rbac::Action::kAllow,
            std::move(policies));
  rbac.audit_condition = Rbac::AuditCondition::kNone;
  rbac.logger_configs.push_back(
      std::make_unique<TestAuditLoggerFactory::Config>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kAllow);
  EXPECT_EQ(decision.matching_policy_name, "policy1");
  EXPECT_EQ(audit_logs_.size(), 0);
}

TEST_F(GrpcAuthorizationEngineTest, AuditLoggerNoneNotInvokedOnDeniedRequest) {
  Rbac::Policy policy1(
      Rbac::Permission::MakeNotPermission(
          Rbac::Permission::MakeAnyPermission()),
      Rbac::Principal::MakeNotPrincipal(Rbac::Principal::MakeAnyPrincipal()));
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  Rbac rbac(std::string(kPolicyName), Rbac::Action::kAllow,
            std::move(policies));
  rbac.audit_condition = Rbac::AuditCondition::kNone;
  rbac.logger_configs.push_back(
      std::make_unique<TestAuditLoggerFactory::Config>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_EQ(decision.matching_policy_name, "");
  EXPECT_EQ(audit_logs_.size(), 0);
}

TEST_F(GrpcAuthorizationEngineTest, AuditLoggerOnDenyNotInvoked) {
  Rbac::Policy policy1(Rbac::Permission::MakeAnyPermission(),
                       Rbac::Principal::MakeAnyPrincipal());
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  Rbac rbac(std::string(kPolicyName), Rbac::Action::kAllow,
            std::move(policies));
  rbac.audit_condition = Rbac::AuditCondition::kOnDeny;
  rbac.logger_configs.push_back(
      std::make_unique<TestAuditLoggerFactory::Config>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kAllow);
  EXPECT_EQ(decision.matching_policy_name, "policy1");
  EXPECT_EQ(audit_logs_.size(), 0);
}

TEST_F(GrpcAuthorizationEngineTest, AuditLoggerOnAllowNotInvoked) {
  Rbac::Policy policy1(
      Rbac::Permission::MakeNotPermission(
          Rbac::Permission::MakeAnyPermission()),
      Rbac::Principal::MakeNotPrincipal(Rbac::Principal::MakeAnyPrincipal()));
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  Rbac rbac(std::string(kPolicyName), Rbac::Action::kAllow,
            std::move(policies));
  rbac.audit_condition = Rbac::AuditCondition::kOnAllow;
  rbac.logger_configs.push_back(
      std::make_unique<TestAuditLoggerFactory::Config>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_EQ(decision.matching_policy_name, "");
  EXPECT_EQ(audit_logs_.size(), 0);
}

TEST_F(GrpcAuthorizationEngineTest, AuditLoggerOnAllowInvoked) {
  Rbac::Policy policy1(Rbac::Permission::MakeAnyPermission(),
                       Rbac::Principal::MakeAnyPrincipal());
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  Rbac rbac(std::string(kPolicyName), Rbac::Action::kAllow,
            std::move(policies));
  rbac.audit_condition = Rbac::AuditCondition::kOnAllow;
  rbac.logger_configs.push_back(
      std::make_unique<TestAuditLoggerFactory::Config>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kAllow);
  EXPECT_EQ(decision.matching_policy_name, "policy1");
  EXPECT_THAT(audit_logs_,
              ::testing::ElementsAre(absl::StrFormat(
                  "{\"authorized\":true,\"matched_rule\":\"policy1\",\"policy_"
                  "name\":\"%s\",\"principal\":\"%s\",\"rpc_method\":\"%s\"}",
                  kPolicyName, kSpiffeId, kRpcMethod)));
}

TEST_F(GrpcAuthorizationEngineTest,
       AuditLoggerOnDenyAndAllowInvokedWithAllowedRequest) {
  Rbac::Policy policy1(Rbac::Permission::MakeAnyPermission(),
                       Rbac::Principal::MakeAnyPrincipal());
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  Rbac rbac(std::string(kPolicyName), Rbac::Action::kAllow,
            std::move(policies));
  rbac.audit_condition = Rbac::AuditCondition::kOnDenyAndAllow;
  rbac.logger_configs.push_back(
      std::make_unique<TestAuditLoggerFactory::Config>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kAllow);
  EXPECT_EQ(decision.matching_policy_name, "policy1");
  EXPECT_THAT(audit_logs_,
              ::testing::ElementsAre(absl::StrFormat(
                  "{\"authorized\":true,\"matched_rule\":\"policy1\",\"policy_"
                  "name\":\"%s\",\"principal\":\"%s\",\"rpc_method\":\"%s\"}",
                  kPolicyName, kSpiffeId, kRpcMethod)));
}

TEST_F(GrpcAuthorizationEngineTest, AuditLoggerOnDenyInvoked) {
  Rbac::Policy policy1(
      Rbac::Permission::MakeNotPermission(
          Rbac::Permission::MakeAnyPermission()),
      Rbac::Principal::MakeNotPrincipal(Rbac::Principal::MakeAnyPrincipal()));
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  Rbac rbac(std::string(kPolicyName), Rbac::Action::kAllow,
            std::move(policies));
  rbac.audit_condition = Rbac::AuditCondition::kOnDeny;
  rbac.logger_configs.push_back(
      std::make_unique<TestAuditLoggerFactory::Config>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_EQ(decision.matching_policy_name, "");
  EXPECT_THAT(audit_logs_,
              ::testing::ElementsAre(absl::StrFormat(
                  "{\"authorized\":false,\"matched_rule\":\"\",\"policy_"
                  "name\":\"%s\",\"principal\":\"%s\",\"rpc_method\":\"%s\"}",
                  kPolicyName, kSpiffeId, kRpcMethod)));
}

TEST_F(GrpcAuthorizationEngineTest,
       AuditLoggerOnDenyAndAllowInvokedWithDeniedRequest) {
  Rbac::Policy policy1(
      Rbac::Permission::MakeNotPermission(
          Rbac::Permission::MakeAnyPermission()),
      Rbac::Principal::MakeNotPrincipal(Rbac::Principal::MakeAnyPrincipal()));
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  Rbac rbac(std::string(kPolicyName), Rbac::Action::kAllow,
            std::move(policies));
  rbac.audit_condition = Rbac::AuditCondition::kOnDenyAndAllow;
  rbac.logger_configs.push_back(
      std::make_unique<TestAuditLoggerFactory::Config>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_EQ(decision.matching_policy_name, "");
  EXPECT_THAT(audit_logs_,
              ::testing::ElementsAre(absl::StrFormat(
                  "{\"authorized\":false,\"matched_rule\":\"\",\"policy_"
                  "name\":\"%s\",\"principal\":\"%s\",\"rpc_method\":\"%s\"}",
                  kPolicyName, kSpiffeId, kRpcMethod)));
}

TEST_F(GrpcAuthorizationEngineTest, MultipleAuditLoggerInvoked) {
  Rbac::Policy policy1(
      Rbac::Permission::MakeNotPermission(
          Rbac::Permission::MakeAnyPermission()),
      Rbac::Principal::MakeNotPrincipal(Rbac::Principal::MakeAnyPrincipal()));
  std::map<std::string, Rbac::Policy> policies;
  policies["policy1"] = std::move(policy1);
  Rbac rbac(std::string(kPolicyName), Rbac::Action::kAllow,
            std::move(policies));
  rbac.audit_condition = Rbac::AuditCondition::kOnDenyAndAllow;
  rbac.logger_configs.push_back(
      std::make_unique<TestAuditLoggerFactory::Config>());
  rbac.logger_configs.push_back(
      std::make_unique<TestAuditLoggerFactory::Config>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_EQ(decision.matching_policy_name, "");
  EXPECT_THAT(
      audit_logs_,
      ::testing::ElementsAre(
          absl::StrFormat(
              "{\"authorized\":false,\"matched_rule\":\"\",\"policy_"
              "name\":\"%s\",\"principal\":\"%s\",\"rpc_method\":\"%s\"}",
              kPolicyName, kSpiffeId, kRpcMethod),
          absl::StrFormat(
              "{\"authorized\":false,\"matched_rule\":\"\",\"policy_"
              "name\":\"%s\",\"principal\":\"%s\",\"rpc_method\":\"%s\"}",
              kPolicyName, kSpiffeId, kRpcMethod)));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
