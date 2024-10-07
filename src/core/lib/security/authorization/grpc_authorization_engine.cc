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

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <map>
#include <utility>

#include "absl/log/check.h"
#include "src/core/lib/security/authorization/audit_logging.h"
#include "src/core/lib/security/authorization/authorization_engine.h"

namespace grpc_core {

using experimental::AuditContext;
using experimental::AuditLoggerRegistry;

namespace {

using Decision = AuthorizationEngine::Decision;

bool ShouldLog(const Decision& decision,
               const Rbac::AuditCondition& condition) {
  return condition == Rbac::AuditCondition::kOnDenyAndAllow ||
         (decision.type == Decision::Type::kAllow &&
          condition == Rbac::AuditCondition::kOnAllow) ||
         (decision.type == Decision::Type::kDeny &&
          condition == Rbac::AuditCondition::kOnDeny);
}

}  // namespace

GrpcAuthorizationEngine::GrpcAuthorizationEngine(Rbac policy)
    : name_(std::move(policy.name)),
      action_(policy.action),
      audit_condition_(policy.audit_condition) {
  for (auto& sub_policy : policy.policies) {
    Policy policy;
    policy.name = sub_policy.first;
    policy.matcher = std::make_unique<PolicyAuthorizationMatcher>(
        std::move(sub_policy.second));
    policies_.push_back(std::move(policy));
  }
  for (auto& logger_config : policy.logger_configs) {
    auto logger =
        AuditLoggerRegistry::CreateAuditLogger(std::move(logger_config));
    CHECK(logger != nullptr);
    audit_loggers_.push_back(std::move(logger));
  }
}

GrpcAuthorizationEngine::GrpcAuthorizationEngine(
    GrpcAuthorizationEngine&& other) noexcept
    : name_(std::move(other.name_)),
      action_(other.action_),
      policies_(std::move(other.policies_)),
      audit_condition_(other.audit_condition_),
      audit_loggers_(std::move(other.audit_loggers_)) {}

GrpcAuthorizationEngine& GrpcAuthorizationEngine::operator=(
    GrpcAuthorizationEngine&& other) noexcept {
  name_ = std::move(other.name_);
  action_ = other.action_;
  policies_ = std::move(other.policies_);
  audit_condition_ = other.audit_condition_;
  audit_loggers_ = std::move(other.audit_loggers_);
  return *this;
}

AuthorizationEngine::Decision GrpcAuthorizationEngine::Evaluate(
    const EvaluateArgs& args) const {
  Decision decision;
  bool matches = false;
  for (const auto& policy : policies_) {
    if (policy.matcher->Matches(args)) {
      matches = true;
      decision.matching_policy_name = policy.name;
      break;
    }
  }
  decision.type = (matches == (action_ == Rbac::Action::kAllow))
                      ? Decision::Type::kAllow
                      : Decision::Type::kDeny;
  if (ShouldLog(decision, audit_condition_)) {
    for (auto& logger : audit_loggers_) {
      logger->Log(AuditContext(args.GetPath(), args.GetSpiffeId(), name_,
                               decision.matching_policy_name,
                               decision.type == Decision::Type::kAllow));
    }
  }
  return decision;
}

}  // namespace grpc_core
