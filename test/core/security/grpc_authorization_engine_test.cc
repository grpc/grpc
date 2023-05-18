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

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/grpc_audit_logging.h>
#include <grpc/grpc_security_constants.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/security/authorization/audit_logging.h"
#include "test/core/util/evaluate_args_test_util.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kLoggerName = "test_logger";
constexpr absl::string_view kPolicyName = "authz";
constexpr absl::string_view kSpiffeId = "spiffe://foo";
constexpr absl::string_view kRpcMethod = "/foo.Bar/Echo";

using experimental::AuditContext;
using experimental::AuditLogger;
using experimental::AuditLoggerFactory;
using experimental::AuditLoggerRegistry;
using experimental::RegisterAuditLoggerFactory;

// This test class copies the audit context.
struct TestAuditContext {
  explicit TestAuditContext(const AuditContext& context)
      : rpc_method(context.rpc_method()),
        principal(context.principal()),
        policy_name(context.policy_name()),
        matched_rule(context.matched_rule()),
        authorized(context.authorized()) {}

  std::string rpc_method;
  std::string principal;
  std::string policy_name;
  std::string matched_rule;
  bool authorized;
};

class TestAuditLogger : public AuditLogger {
 public:
  explicit TestAuditLogger(
      std::vector<std::unique_ptr<TestAuditContext>>* contexts)
      : contexts_(contexts) {}

  void Log(const AuditContext& context) override {
    contexts_->push_back(std::make_unique<TestAuditContext>(context));
  }

 private:
  std::vector<std::unique_ptr<TestAuditContext>>* contexts_;
};

class TestAuditLoggerFactory : public AuditLoggerFactory {
 public:
  class TestAuditLoggerConfig : public AuditLoggerFactory::Config {
    absl::string_view name() const override { return kLoggerName; }
    std::string ToString() const override { return ""; }
  };

  explicit TestAuditLoggerFactory(
      std::vector<std::unique_ptr<TestAuditContext>>* contexts)
      : contexts_(contexts) {}

  absl::string_view name() const override { return kLoggerName; }
  absl::StatusOr<std::unique_ptr<AuditLoggerFactory::Config>>
  ParseAuditLoggerConfig(const Json&) override {
    Crash("unreachable");
    return nullptr;
  }
  std::unique_ptr<AuditLogger> CreateAuditLogger(
      std::unique_ptr<AuditLoggerFactory::Config>) override {
    return std::make_unique<TestAuditLogger>(contexts_);
  }

 private:
  std::vector<std::unique_ptr<TestAuditContext>>* contexts_;
};

class GrpcAuthorizationEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    RegisterAuditLoggerFactory(
        std::make_unique<TestAuditLoggerFactory>(&contexts_));
    evaluate_args_util_.AddPairToMetadata(":path", kRpcMethod.data());
    evaluate_args_util_.AddPropertyToAuthContext(
        GRPC_PEER_SPIFFE_ID_PROPERTY_NAME, kSpiffeId.data());
  }

  void TearDown() override { AuditLoggerRegistry::TestOnlyResetRegistry(); }

  std::vector<std::unique_ptr<TestAuditContext>> contexts_;
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
      std::make_unique<TestAuditLoggerFactory::TestAuditLoggerConfig>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kAllow);
  EXPECT_EQ(decision.matching_policy_name, "policy1");
  EXPECT_EQ(contexts_.size(), 0);
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
      std::make_unique<TestAuditLoggerFactory::TestAuditLoggerConfig>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_EQ(decision.matching_policy_name, "");
  EXPECT_EQ(contexts_.size(), 0);
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
      std::make_unique<TestAuditLoggerFactory::TestAuditLoggerConfig>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kAllow);
  EXPECT_EQ(decision.matching_policy_name, "policy1");
  EXPECT_EQ(contexts_.size(), 0);
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
      std::make_unique<TestAuditLoggerFactory::TestAuditLoggerConfig>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_EQ(decision.matching_policy_name, "");
  EXPECT_EQ(contexts_.size(), 0);
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
      std::make_unique<TestAuditLoggerFactory::TestAuditLoggerConfig>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kAllow);
  EXPECT_EQ(decision.matching_policy_name, "policy1");
  ASSERT_EQ(contexts_.size(), 1);
  EXPECT_EQ(contexts_[0]->rpc_method, kRpcMethod);
  EXPECT_EQ(contexts_[0]->principal, kSpiffeId);
  EXPECT_EQ(contexts_[0]->policy_name, kPolicyName);
  EXPECT_EQ(contexts_[0]->matched_rule, "policy1");
  EXPECT_EQ(contexts_[0]->authorized, true);
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
      std::make_unique<TestAuditLoggerFactory::TestAuditLoggerConfig>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kAllow);
  EXPECT_EQ(decision.matching_policy_name, "policy1");
  ASSERT_EQ(contexts_.size(), 1);
  EXPECT_EQ(contexts_[0]->rpc_method, kRpcMethod);
  EXPECT_EQ(contexts_[0]->principal, kSpiffeId);
  EXPECT_EQ(contexts_[0]->policy_name, kPolicyName);
  EXPECT_EQ(contexts_[0]->matched_rule, "policy1");
  EXPECT_EQ(contexts_[0]->authorized, true);
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
      std::make_unique<TestAuditLoggerFactory::TestAuditLoggerConfig>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_EQ(decision.matching_policy_name, "");
  ASSERT_EQ(contexts_.size(), 1);
  EXPECT_EQ(contexts_[0]->rpc_method, kRpcMethod);
  EXPECT_EQ(contexts_[0]->principal, kSpiffeId);
  EXPECT_EQ(contexts_[0]->policy_name, kPolicyName);
  EXPECT_EQ(contexts_[0]->matched_rule, "");
  EXPECT_EQ(contexts_[0]->authorized, false);
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
      std::make_unique<TestAuditLoggerFactory::TestAuditLoggerConfig>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_EQ(decision.matching_policy_name, "");
  ASSERT_EQ(contexts_.size(), 1);
  EXPECT_EQ(contexts_[0]->rpc_method, kRpcMethod);
  EXPECT_EQ(contexts_[0]->principal, kSpiffeId);
  EXPECT_EQ(contexts_[0]->policy_name, kPolicyName);
  EXPECT_EQ(contexts_[0]->matched_rule, "");
  EXPECT_EQ(contexts_[0]->authorized, false);
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
      std::make_unique<TestAuditLoggerFactory::TestAuditLoggerConfig>());
  rbac.logger_configs.push_back(
      std::make_unique<TestAuditLoggerFactory::TestAuditLoggerConfig>());
  GrpcAuthorizationEngine engine(std::move(rbac));
  AuthorizationEngine::Decision decision =
      engine.Evaluate(evaluate_args_util_.MakeEvaluateArgs());
  EXPECT_EQ(decision.type, AuthorizationEngine::Decision::Type::kDeny);
  EXPECT_EQ(decision.matching_policy_name, "");
  ASSERT_EQ(contexts_.size(), 2);
  for (const auto& context : contexts_) {
    EXPECT_EQ(context->rpc_method, kRpcMethod);
    EXPECT_EQ(context->principal, kSpiffeId);
    EXPECT_EQ(context->policy_name, kPolicyName);
    EXPECT_EQ(context->matched_rule, "");
    EXPECT_EQ(context->authorized, false);
  }
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
