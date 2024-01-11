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

#include <string.h>

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_security_constants.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"

namespace grpc_core {

std::unique_ptr<AuthorizationMatcher> AuthorizationMatcher::Create(
    Rbac::Permission permission) {
  switch (permission.type) {
    case Rbac::Permission::RuleType::kAnd: {
      std::vector<std::unique_ptr<AuthorizationMatcher>> matchers;
      matchers.reserve(permission.permissions.size());
      for (const auto& rule : permission.permissions) {
        matchers.push_back(AuthorizationMatcher::Create(std::move(*rule)));
      }
      return std::make_unique<AndAuthorizationMatcher>(std::move(matchers));
    }
    case Rbac::Permission::RuleType::kOr: {
      std::vector<std::unique_ptr<AuthorizationMatcher>> matchers;
      matchers.reserve(permission.permissions.size());
      for (const auto& rule : permission.permissions) {
        matchers.push_back(AuthorizationMatcher::Create(std::move(*rule)));
      }
      return std::make_unique<OrAuthorizationMatcher>(std::move(matchers));
    }
    case Rbac::Permission::RuleType::kNot:
      return std::make_unique<NotAuthorizationMatcher>(
          AuthorizationMatcher::Create(std::move(*permission.permissions[0])));
    case Rbac::Permission::RuleType::kAny:
      return std::make_unique<AlwaysAuthorizationMatcher>();
    case Rbac::Permission::RuleType::kHeader:
      return std::make_unique<HeaderAuthorizationMatcher>(
          std::move(permission.header_matcher));
    case Rbac::Permission::RuleType::kPath:
      return std::make_unique<PathAuthorizationMatcher>(
          std::move(permission.string_matcher));
    case Rbac::Permission::RuleType::kDestIp:
      return std::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kDestIp, std::move(permission.ip));
    case Rbac::Permission::RuleType::kDestPort:
      return std::make_unique<PortAuthorizationMatcher>(permission.port);
    case Rbac::Permission::RuleType::kMetadata:
      return std::make_unique<MetadataAuthorizationMatcher>(permission.invert);
    case Rbac::Permission::RuleType::kReqServerName:
      return std::make_unique<ReqServerNameAuthorizationMatcher>(
          std::move(permission.string_matcher));
  }
  return nullptr;
}

std::unique_ptr<AuthorizationMatcher> AuthorizationMatcher::Create(
    Rbac::Principal principal) {
  switch (principal.type) {
    case Rbac::Principal::RuleType::kAnd: {
      std::vector<std::unique_ptr<AuthorizationMatcher>> matchers;
      matchers.reserve(principal.principals.size());
      for (const auto& id : principal.principals) {
        matchers.push_back(AuthorizationMatcher::Create(std::move(*id)));
      }
      return std::make_unique<AndAuthorizationMatcher>(std::move(matchers));
    }
    case Rbac::Principal::RuleType::kOr: {
      std::vector<std::unique_ptr<AuthorizationMatcher>> matchers;
      matchers.reserve(principal.principals.size());
      for (const auto& id : principal.principals) {
        matchers.push_back(AuthorizationMatcher::Create(std::move(*id)));
      }
      return std::make_unique<OrAuthorizationMatcher>(std::move(matchers));
    }
    case Rbac::Principal::RuleType::kNot:
      return std::make_unique<NotAuthorizationMatcher>(
          AuthorizationMatcher::Create(std::move(*principal.principals[0])));
    case Rbac::Principal::RuleType::kAny:
      return std::make_unique<AlwaysAuthorizationMatcher>();
    case Rbac::Principal::RuleType::kPrincipalName:
      return std::make_unique<AuthenticatedAuthorizationMatcher>(
          std::move(principal.string_matcher));
    case Rbac::Principal::RuleType::kSourceIp:
      return std::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kSourceIp, std::move(principal.ip));
    case Rbac::Principal::RuleType::kDirectRemoteIp:
      return std::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kDirectRemoteIp,
          std::move(principal.ip));
    case Rbac::Principal::RuleType::kRemoteIp:
      return std::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kRemoteIp, std::move(principal.ip));
    case Rbac::Principal::RuleType::kHeader:
      return std::make_unique<HeaderAuthorizationMatcher>(
          std::move(principal.header_matcher));
    case Rbac::Principal::RuleType::kPath:
      return std::make_unique<PathAuthorizationMatcher>(
          std::move(principal.string_matcher.value()));
    case Rbac::Principal::RuleType::kMetadata:
      return std::make_unique<MetadataAuthorizationMatcher>(principal.invert);
  }
  return nullptr;
}

bool AndAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  for (const auto& matcher : matchers_) {
    if (!matcher->Matches(args)) {
      return false;
    }
  }
  return true;
}

bool OrAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  for (const auto& matcher : matchers_) {
    if (matcher->Matches(args)) {
      return true;
    }
  }
  return false;
}

bool NotAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  return !matcher_->Matches(args);
}

bool HeaderAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  std::string concatenated_value;
  return matcher_.Match(
      args.GetHeaderValue(matcher_.name(), &concatenated_value));
}

IpAuthorizationMatcher::IpAuthorizationMatcher(Type type, Rbac::CidrRange range)
    : type_(type), prefix_len_(range.prefix_len) {
  auto address =
      StringToSockaddr(range.address_prefix, 0);  // Port does not matter here.
  if (!address.ok()) {
    gpr_log(GPR_DEBUG, "CidrRange address \"%s\" is not IPv4/IPv6. Error: %s",
            range.address_prefix.c_str(), address.status().ToString().c_str());
    memset(&subnet_address_, 0, sizeof(subnet_address_));
    return;
  }
  subnet_address_ = *address;
  grpc_sockaddr_mask_bits(&subnet_address_, prefix_len_);
}

bool IpAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  grpc_resolved_address address;
  switch (type_) {
    case Type::kDestIp: {
      address = args.GetLocalAddress();
      break;
    }
    case Type::kSourceIp:
    case Type::kDirectRemoteIp:
    case Type::kRemoteIp: {
      address = args.GetPeerAddress();
      break;
    }
    default:
      return false;
  }
  return grpc_sockaddr_match_subnet(&address, &subnet_address_, prefix_len_);
}

bool PortAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  return port_ == args.GetLocalPort();
}

bool AuthenticatedAuthorizationMatcher::Matches(
    const EvaluateArgs& args) const {
  if (args.GetTransportSecurityType() != GRPC_SSL_TRANSPORT_SECURITY_TYPE &&
      args.GetTransportSecurityType() != GRPC_TLS_TRANSPORT_SECURITY_TYPE) {
    // Connection is not authenticated.
    return false;
  }
  if (!matcher_.has_value()) {
    // Allows any authenticated user.
    return true;
  }
  std::vector<absl::string_view> uri_sans = args.GetUriSans();
  if (!uri_sans.empty()) {
    for (const auto& uri : uri_sans) {
      if (matcher_->Match(uri)) {
        return true;
      }
    }
  }
  std::vector<absl::string_view> dns_sans = args.GetDnsSans();
  if (!dns_sans.empty()) {
    for (const auto& dns : dns_sans) {
      if (matcher_->Match(dns)) {
        return true;
      }
    }
  }
  return matcher_->Match(args.GetSubject());
}

bool ReqServerNameAuthorizationMatcher::Matches(const EvaluateArgs&) const {
  // Currently we only support matching against an empty string.
  return matcher_.Match("");
}

bool PathAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  absl::string_view path = args.GetPath();
  if (!path.empty()) {
    return matcher_.Match(path);
  }
  return false;
}

bool PolicyAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  return permissions_->Matches(args) && principals_->Matches(args);
}

}  // namespace grpc_core
