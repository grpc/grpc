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

#include "src/core/lib/security/authorization/matchers.h"

namespace grpc_core {

namespace {

bool AuthenticatedMatchesHelper(const EvaluateArgs& args,
                                const StringMatcher& matcher) {
  if (args.GetTransportSecurityType() != GRPC_SSL_TRANSPORT_SECURITY_TYPE) {
    // Connection is not authenticated.
    return false;
  }
  if (matcher.string_matcher().empty()) {
    // Allows any authenticated user.
    return true;
  }
  absl::string_view spiffe_id = args.GetSpiffeId();
  if (!spiffe_id.empty()) {
    return matcher.Match(spiffe_id);
  }
  // TODO(ashithasantosh): Check principal matches DNS SAN, followed by Subject
  // field from certificate. This requires updating tsi_peer to expose these
  // fields.
  return false;
}

}  // namespace

std::unique_ptr<AuthorizationMatcher> AuthorizationMatcher::Create(
    Rbac::Permission permission) {
  switch (permission.type) {
    case Rbac::Permission::RuleType::kAnd:
      return absl::make_unique<AndAuthorizationMatcher>(
          std::move(permission.permissions), permission.not_rule);
    case Rbac::Permission::RuleType::kOr:
      return absl::make_unique<OrAuthorizationMatcher>(
          std::move(permission.permissions), permission.not_rule);
    case Rbac::Permission::RuleType::kAny:
      return absl::make_unique<AlwaysAuthorizationMatcher>(permission.not_rule);
    case Rbac::Permission::RuleType::kHeader:
      return absl::make_unique<HeaderAuthorizationMatcher>(
          std::move(permission.header_matcher), permission.not_rule);
    case Rbac::Permission::RuleType::kPath:
      return absl::make_unique<PathAuthorizationMatcher>(
          std::move(permission.string_matcher), permission.not_rule);
    case Rbac::Permission::RuleType::kDestIp:
      return absl::make_unique<IpAuthorizationMatcher>(std::move(permission.ip),
                                                       permission.not_rule);
    case Rbac::Permission::RuleType::kDestPort:
      return absl::make_unique<PortAuthorizationMatcher>(permission.port,
                                                         permission.not_rule);
    case Rbac::Permission::RuleType::kReqServerName:
      return absl::make_unique<ReqServerNameAuthorizationMatcher>(
          std::move(permission.string_matcher), permission.not_rule);
  }
  return nullptr;
}

std::unique_ptr<AuthorizationMatcher> AuthorizationMatcher::Create(
    Rbac::Principal principal) {
  switch (principal.type) {
    case Rbac::Principal::RuleType::kAnd:
      return absl::make_unique<AndAuthorizationMatcher>(
          std::move(principal.principals), principal.not_rule);
    case Rbac::Principal::RuleType::kOr:
      return absl::make_unique<OrAuthorizationMatcher>(
          std::move(principal.principals), principal.not_rule);
    case Rbac::Principal::RuleType::kAny:
      return absl::make_unique<AlwaysAuthorizationMatcher>(principal.not_rule);
    case Rbac::Principal::RuleType::kPrincipalName:
      return absl::make_unique<AuthenticatedAuthorizationMatcher>(
          std::move(principal.string_matcher), principal.not_rule);
    case Rbac::Principal::RuleType::kSourceIp:
    case Rbac::Principal::RuleType::kDirectRemoteIp:
    case Rbac::Principal::RuleType::kRemoteIp:
      return absl::make_unique<IpAuthorizationMatcher>(std::move(principal.ip),
                                                       principal.not_rule);
    case Rbac::Principal::RuleType::kHeader:
      return absl::make_unique<HeaderAuthorizationMatcher>(
          std::move(principal.header_matcher), principal.not_rule);
    case Rbac::Principal::RuleType::kPath:
      return absl::make_unique<PathAuthorizationMatcher>(
          std::move(principal.string_matcher), principal.not_rule);
  }
  return nullptr;
}

AndAuthorizationMatcher::AndAuthorizationMatcher(
    std::vector<std::unique_ptr<Rbac::Permission>> rules, bool not_rule)
    : not_rule_(not_rule) {
  for (auto& rule : rules) {
    matchers_.push_back(AuthorizationMatcher::Create(std::move(*rule)));
  }
}

AndAuthorizationMatcher::AndAuthorizationMatcher(
    std::vector<std::unique_ptr<Rbac::Principal>> ids, bool not_rule)
    : not_rule_(not_rule) {
  for (const auto& id : ids) {
    matchers_.push_back(AuthorizationMatcher::Create(std::move(*id)));
  }
}

bool AndAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  bool matches = true;
  for (const auto& matcher : matchers_) {
    if (!matcher->Matches(args)) {
      matches = false;
      break;
    }
  }
  return matches != not_rule_;
}

OrAuthorizationMatcher::OrAuthorizationMatcher(
    std::vector<std::unique_ptr<Rbac::Permission>> rules, bool not_rule)
    : not_rule_(not_rule) {
  for (const auto& rule : rules) {
    matchers_.push_back(AuthorizationMatcher::Create(std::move(*rule)));
  }
}

OrAuthorizationMatcher::OrAuthorizationMatcher(
    std::vector<std::unique_ptr<Rbac::Principal>> ids, bool not_rule)
    : not_rule_(not_rule) {
  for (const auto& id : ids) {
    matchers_.push_back(AuthorizationMatcher::Create(std::move(*id)));
  }
}

bool OrAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  bool matches = false;
  for (const auto& matcher : matchers_) {
    if (matcher->Matches(args)) {
      matches = true;
      break;
    }
  }
  return matches != not_rule_;
}

bool HeaderAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  std::string concatenated_value;
  bool matches =
      matcher_.Match(args.GetHeaderValue(matcher_.name(), &concatenated_value));
  return matches != not_rule_;
}

// TODO(ashithasantosh): Implement IpAuthorizationMatcher::Matches.
bool IpAuthorizationMatcher::Matches(const EvaluateArgs&) const {
  bool matches = false;
  return matches != not_rule_;
}

bool PortAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  bool matches = (port_ == args.GetLocalPort());
  return matches != not_rule_;
}

bool AuthenticatedAuthorizationMatcher::Matches(
    const EvaluateArgs& args) const {
  bool matches = AuthenticatedMatchesHelper(args, matcher_);
  return matches != not_rule_;
}

bool ReqServerNameAuthorizationMatcher::Matches(const EvaluateArgs&) const {
  // Currently we do not support matching rules containing
  // "requested_server_name".
  bool matches = false;
  return matches != not_rule_;
}

bool PathAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  bool matches = false;
  absl::string_view path = args.GetPath();
  if (!path.empty()) {
    matches = matcher_.Match(path);
  }
  return matches != not_rule_;
}

bool PolicyAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  return permissions_->Matches(args) && principals_->Matches(args);
}

}  // namespace grpc_core
