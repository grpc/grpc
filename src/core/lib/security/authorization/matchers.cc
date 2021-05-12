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

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/security/authorization/matchers.h"

namespace grpc_core {

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
      return absl::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kDestIp, std::move(permission.ip),
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
      return absl::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kSourceIp, std::move(principal.ip),
          principal.not_rule);
    case Rbac::Principal::RuleType::kDirectRemoteIp:
      return absl::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kDirectRemoteIp,
          std::move(principal.ip), principal.not_rule);
    case Rbac::Principal::RuleType::kRemoteIp:
      return absl::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kRemoteIp, std::move(principal.ip),
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

IpAuthorizationMatcher::IpAuthorizationMatcher(Type type, Rbac::CidrRange range,
                                               bool not_rule)
    : type_(type), prefix_len_(range.prefix_len), not_rule_(not_rule) {
  grpc_error_handle error =
      grpc_string_to_sockaddr(&subnet_address_, range.address_prefix.c_str(),
                              /*port does not matter here*/ 0);
  if (error == GRPC_ERROR_NONE) {
    grpc_sockaddr_mask_bits(&subnet_address_, prefix_len_);
  } else {
    gpr_log(GPR_DEBUG, "CidrRange address %s is not IPv4/IPv6. Error: %s",
            range.address_prefix.c_str(), grpc_error_std_string(error).c_str());
  }
  GRPC_ERROR_UNREF(error);
}

bool IpAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  grpc_resolved_address address;
  switch (type_) {
    case Type::kDestIp: {
      address = args.GetLocalAddress();
      break;
    }
    case Type::kSourceIp:
    case Type::kDirectRemoteIp: {
      address = args.GetPeerAddress();
      break;
    }
    default: {
      // Currently we do not support matching rules containing "remote_ip".
      return not_rule_;
    }
  }
  bool matches =
      grpc_sockaddr_match_subnet(&address, &subnet_address_, prefix_len_);
  return matches != not_rule_;
}

bool PortAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  bool matches = (port_ == args.GetLocalPort());
  return matches != not_rule_;
}

bool AuthenticatedAuthorizationMatcher::Matches(
    const EvaluateArgs& args) const {
  if (args.GetTransportSecurityType() != GRPC_SSL_TRANSPORT_SECURITY_TYPE) {
    // Connection is not authenticated.
    return not_rule_;
  }
  if (matcher_.string_matcher().empty()) {
    // Allows any authenticated user.
    return !not_rule_;
  }
  absl::string_view spiffe_id = args.GetSpiffeId();
  if (!spiffe_id.empty()) {
    return matcher_.Match(spiffe_id) != not_rule_;
  }
  std::vector<absl::string_view> dns_sans = args.GetDnsSans();
  if (!dns_sans.empty()) {
    for (const auto& dns : dns_sans) {
      if (matcher_.Match(dns)) {
        return !not_rule_;
      }
    }
  }
  // TODO(ashithasantosh): Check Subject field from certificate.
  return not_rule_;
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
