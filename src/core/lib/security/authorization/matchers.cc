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

#include <grpc/grpc_security_constants.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"

namespace grpc_core {

std::unique_ptr<AuthorizationMatcher> AuthorizationMatcher::Create(
    Rbac::Permission permission) {
  switch (permission.type) {
    case Rbac::Permission::RuleType::kAnd: {
      std::vector<std::unique_ptr<AuthorizationMatcher>> matchers;
      for (const auto& rule : permission.permissions) {
        matchers.push_back(AuthorizationMatcher::Create(std::move(*rule)));
      }
      return absl::make_unique<AndAuthorizationMatcher>(std::move(matchers));
    }
    case Rbac::Permission::RuleType::kOr: {
      std::vector<std::unique_ptr<AuthorizationMatcher>> matchers;
      for (const auto& rule : permission.permissions) {
        matchers.push_back(AuthorizationMatcher::Create(std::move(*rule)));
      }
      return absl::make_unique<OrAuthorizationMatcher>(std::move(matchers));
    }
    case Rbac::Permission::RuleType::kNot:
      return absl::make_unique<NotAuthorizationMatcher>(
          AuthorizationMatcher::Create(std::move(*permission.permissions[0])));
    case Rbac::Permission::RuleType::kAny:
      return absl::make_unique<AlwaysAuthorizationMatcher>();
    case Rbac::Permission::RuleType::kHeader:
      return absl::make_unique<HeaderAuthorizationMatcher>(
          std::move(permission.header_matcher));
    case Rbac::Permission::RuleType::kPath:
      return absl::make_unique<PathAuthorizationMatcher>(
          std::move(permission.string_matcher));
    case Rbac::Permission::RuleType::kDestIp:
      return absl::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kDestIp, std::move(permission.ip));
    case Rbac::Permission::RuleType::kDestPort:
      return absl::make_unique<PortAuthorizationMatcher>(permission.port);
    case Rbac::Permission::RuleType::kMetadata:
      return absl::make_unique<MetadataAuthorizationMatcher>(permission.invert);
    case Rbac::Permission::RuleType::kReqServerName:
      return absl::make_unique<ReqServerNameAuthorizationMatcher>(
          std::move(permission.string_matcher));
  }
  return nullptr;
}

std::unique_ptr<AuthorizationMatcher> AuthorizationMatcher::Create(
    Rbac::Principal principal) {
  switch (principal.type) {
    case Rbac::Principal::RuleType::kAnd: {
      std::vector<std::unique_ptr<AuthorizationMatcher>> matchers;
      for (const auto& id : principal.principals) {
        matchers.push_back(AuthorizationMatcher::Create(std::move(*id)));
      }
      return absl::make_unique<AndAuthorizationMatcher>(std::move(matchers));
    }
    case Rbac::Principal::RuleType::kOr: {
      std::vector<std::unique_ptr<AuthorizationMatcher>> matchers;
      for (const auto& id : principal.principals) {
        matchers.push_back(AuthorizationMatcher::Create(std::move(*id)));
      }
      return absl::make_unique<OrAuthorizationMatcher>(std::move(matchers));
    }
    case Rbac::Principal::RuleType::kNot:
      return absl::make_unique<NotAuthorizationMatcher>(
          AuthorizationMatcher::Create(std::move(*principal.principals[0])));
    case Rbac::Principal::RuleType::kAny:
      return absl::make_unique<AlwaysAuthorizationMatcher>();
    case Rbac::Principal::RuleType::kPrincipalName:
      return absl::make_unique<AuthenticatedAuthorizationMatcher>(
          std::move(principal.string_matcher));
    case Rbac::Principal::RuleType::kSourceIp:
      return absl::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kSourceIp, std::move(principal.ip));
    case Rbac::Principal::RuleType::kDirectRemoteIp:
      return absl::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kDirectRemoteIp,
          std::move(principal.ip));
    case Rbac::Principal::RuleType::kRemoteIp:
      return absl::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kRemoteIp, std::move(principal.ip));
    case Rbac::Principal::RuleType::kHeader:
      return absl::make_unique<HeaderAuthorizationMatcher>(
          std::move(principal.header_matcher));
    case Rbac::Principal::RuleType::kPath:
      return absl::make_unique<PathAuthorizationMatcher>(
          std::move(principal.string_matcher));
    case Rbac::Principal::RuleType::kMetadata:
      return absl::make_unique<MetadataAuthorizationMatcher>(principal.invert);
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
  if (matcher_.string_matcher().empty()) {
    // Allows any authenticated user.
    return true;
  }
  std::vector<absl::string_view> uri_sans = args.GetUriSans();
  if (!uri_sans.empty()) {
    for (const auto& uri : uri_sans) {
      if (matcher_.Match(uri)) {
        return true;
      }
    }
  }
  std::vector<absl::string_view> dns_sans = args.GetDnsSans();
  if (!dns_sans.empty()) {
    for (const auto& dns : dns_sans) {
      if (matcher_.Match(dns)) {
        return true;
      }
    }
  }
  return matcher_.Match(args.GetSubject());
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
