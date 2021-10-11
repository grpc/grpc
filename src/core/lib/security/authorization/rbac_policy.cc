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

#include "src/core/lib/security/authorization/rbac_policy.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

//
// Rbac
//

Rbac::Rbac(Rbac::Action action, std::map<std::string, Policy> policies)
    : action(action), policies(std::move(policies)) {}

Rbac::Rbac(Rbac&& other) noexcept
    : action(other.action), policies(std::move(other.policies)) {}

Rbac& Rbac::operator=(Rbac&& other) noexcept {
  action = other.action;
  policies = std::move(other.policies);
  return *this;
}

std::string Rbac::ToString() const {
  std::vector<std::string> contents;
  contents.push_back(absl::StrFormat(
      "Rbac action=%s{", action == Rbac::Action::kAllow ? "Allow" : "Deny"));
  for (const auto& p : policies) {
    contents.push_back(absl::StrFormat("{\n  policy_name=%s\n%s\n}", p.first,
                                       p.second.ToString()));
  }
  contents.push_back("}");
  return absl::StrJoin(contents, "\n");
}

//
// CidrRange
//

Rbac::CidrRange::CidrRange(std::string address_prefix, uint32_t prefix_len)
    : address_prefix(std::move(address_prefix)), prefix_len(prefix_len) {}

Rbac::CidrRange::CidrRange(Rbac::CidrRange&& other) noexcept
    : address_prefix(std::move(other.address_prefix)),
      prefix_len(other.prefix_len) {}

Rbac::CidrRange& Rbac::CidrRange::operator=(Rbac::CidrRange&& other) noexcept {
  address_prefix = std::move(other.address_prefix);
  prefix_len = other.prefix_len;
  return *this;
}

std::string Rbac::CidrRange::ToString() const {
  return absl::StrFormat("CidrRange{address_prefix=%s,prefix_len=%d}",
                         address_prefix, prefix_len);
}

//
// Permission
//

Rbac::Permission::Permission(
    Permission::RuleType type,
    std::vector<std::unique_ptr<Permission>> permissions)
    : type(type), permissions(std::move(permissions)) {}
Rbac::Permission::Permission(Permission::RuleType type, Permission permission)
    : type(type) {
  permissions.push_back(
      absl::make_unique<Rbac::Permission>(std::move(permission)));
}
Rbac::Permission::Permission(Permission::RuleType type) : type(type) {}
Rbac::Permission::Permission(Permission::RuleType type,
                             HeaderMatcher header_matcher)
    : type(type), header_matcher(std::move(header_matcher)) {}
Rbac::Permission::Permission(Permission::RuleType type,
                             StringMatcher string_matcher)
    : type(type), string_matcher(std::move(string_matcher)) {}
Rbac::Permission::Permission(Permission::RuleType type, CidrRange ip)
    : type(type), ip(std::move(ip)) {}
Rbac::Permission::Permission(Permission::RuleType type, int port)
    : type(type), port(port) {}

Rbac::Permission::Permission(Rbac::Permission&& other) noexcept
    : type(other.type) {
  switch (type) {
    case RuleType::kAnd:
    case RuleType::kOr:
    case RuleType::kNot:
      permissions = std::move(other.permissions);
      break;
    case RuleType::kAny:
      break;
    case RuleType::kHeader:
      header_matcher = std::move(other.header_matcher);
      break;
    case RuleType::kPath:
    case RuleType::kReqServerName:
      string_matcher = std::move(other.string_matcher);
      break;
    case RuleType::kDestIp:
      ip = std::move(other.ip);
      break;
    default:
      port = other.port;
  }
}

Rbac::Permission& Rbac::Permission::operator=(
    Rbac::Permission&& other) noexcept {
  type = other.type;
  switch (type) {
    case RuleType::kAnd:
    case RuleType::kOr:
    case RuleType::kNot:
      permissions = std::move(other.permissions);
      break;
    case RuleType::kAny:
      break;
    case RuleType::kHeader:
      header_matcher = std::move(other.header_matcher);
      break;
    case RuleType::kPath:
    case RuleType::kReqServerName:
      string_matcher = std::move(other.string_matcher);
      break;
    case RuleType::kDestIp:
      ip = std::move(other.ip);
      break;
    default:
      port = other.port;
  }
  return *this;
}

std::string Rbac::Permission::ToString() const {
  switch (type) {
    case RuleType::kAnd: {
      std::vector<std::string> contents;
      contents.reserve(permissions.size());
      for (const auto& permission : permissions) {
        contents.push_back(permission->ToString());
      }
      return absl::StrFormat("and=[%s]", absl::StrJoin(contents, ","));
    }
    case RuleType::kOr: {
      std::vector<std::string> contents;
      contents.reserve(permissions.size());
      for (const auto& permission : permissions) {
        contents.push_back(permission->ToString());
      }
      return absl::StrFormat("or=[%s]", absl::StrJoin(contents, ","));
    }
    case RuleType::kNot:
      return absl::StrFormat("not %s", permissions[0]->ToString());
    case RuleType::kAny:
      return "any";
    case RuleType::kHeader:
      return absl::StrFormat("header=%s", header_matcher.ToString());
    case RuleType::kPath:
      return absl::StrFormat("path=%s", string_matcher.ToString());
    case RuleType::kDestIp:
      return absl::StrFormat("dest_ip=%s", ip.ToString());
    case RuleType::kDestPort:
      return absl::StrFormat("dest_port=%d", port);
    case RuleType::kReqServerName:
      return absl::StrFormat("requested_server_name=%s",
                             string_matcher.ToString());
    default:
      return "";
  }
}

//
// Principal
//

Rbac::Principal::Principal(Principal::RuleType type,
                           std::vector<std::unique_ptr<Principal>> principals)
    : type(type), principals(std::move(principals)) {}
Rbac::Principal::Principal(Principal::RuleType type, Principal principal)
    : type(type) {
  principals.push_back(
      absl::make_unique<Rbac::Principal>(std::move(principal)));
}
Rbac::Principal::Principal(Principal::RuleType type) : type(type) {}
Rbac::Principal::Principal(Principal::RuleType type,
                           StringMatcher string_matcher)
    : type(type), string_matcher(std::move(string_matcher)) {}
Rbac::Principal::Principal(Principal::RuleType type, CidrRange ip)
    : type(type), ip(std::move(ip)) {}
Rbac::Principal::Principal(Principal::RuleType type,
                           HeaderMatcher header_matcher)
    : type(type), header_matcher(std::move(header_matcher)) {}

Rbac::Principal::Principal(Rbac::Principal&& other) noexcept
    : type(other.type) {
  switch (type) {
    case RuleType::kAnd:
    case RuleType::kOr:
    case RuleType::kNot:
      principals = std::move(other.principals);
      break;
    case RuleType::kAny:
      break;
    case RuleType::kHeader:
      header_matcher = std::move(other.header_matcher);
      break;
    case RuleType::kPrincipalName:
    case RuleType::kPath:
      string_matcher = std::move(other.string_matcher);
      break;
    default:
      ip = std::move(other.ip);
  }
}

Rbac::Principal& Rbac::Principal::operator=(Rbac::Principal&& other) noexcept {
  type = other.type;
  switch (type) {
    case RuleType::kAnd:
    case RuleType::kOr:
    case RuleType::kNot:
      principals = std::move(other.principals);
      break;
    case RuleType::kAny:
      break;
    case RuleType::kHeader:
      header_matcher = std::move(other.header_matcher);
      break;
    case RuleType::kPrincipalName:
    case RuleType::kPath:
      string_matcher = std::move(other.string_matcher);
      break;
    default:
      ip = std::move(other.ip);
  }
  return *this;
}

std::string Rbac::Principal::ToString() const {
  switch (type) {
    case RuleType::kAnd: {
      std::vector<std::string> contents;
      contents.reserve(principals.size());
      for (const auto& principal : principals) {
        contents.push_back(principal->ToString());
      }
      return absl::StrFormat("and=[%s]", absl::StrJoin(contents, ","));
    }
    case RuleType::kOr: {
      std::vector<std::string> contents;
      contents.reserve(principals.size());
      for (const auto& principal : principals) {
        contents.push_back(principal->ToString());
      }
      return absl::StrFormat("or=[%s]", absl::StrJoin(contents, ","));
    }
    case RuleType::kNot:
      return absl::StrFormat("not %s", principals[0]->ToString());
    case RuleType::kAny:
      return "any";
    case RuleType::kPrincipalName:
      return absl::StrFormat("principal_name=%s", string_matcher.ToString());
    case RuleType::kSourceIp:
      return absl::StrFormat("source_ip=%s", ip.ToString());
    case RuleType::kDirectRemoteIp:
      return absl::StrFormat("direct_remote_ip=%s", ip.ToString());
    case RuleType::kRemoteIp:
      return absl::StrFormat("remote_ip=%s", ip.ToString());
    case RuleType::kHeader:
      return absl::StrFormat("header=%s", header_matcher.ToString());
    case RuleType::kPath:
      return absl::StrFormat("path=%s", string_matcher.ToString());
    default:
      return "";
  }
}

//
// Policy
//

Rbac::Policy::Policy(Permission permissions, Principal principals)
    : permissions(std::move(permissions)), principals(std::move(principals)) {}

Rbac::Policy::Policy(Rbac::Policy&& other) noexcept
    : permissions(std::move(other.permissions)),
      principals(std::move(other.principals)) {}

Rbac::Policy& Rbac::Policy::operator=(Rbac::Policy&& other) noexcept {
  permissions = std::move(other.permissions);
  principals = std::move(other.principals);
  return *this;
}

std::string Rbac::Policy::ToString() const {
  return absl::StrFormat(
      "  Policy  {\n    Permissions{%s}\n    Principals{%s}\n  }",
      permissions.ToString(), principals.ToString());
}

}  // namespace grpc_core
