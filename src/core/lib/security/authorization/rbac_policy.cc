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

#include "src/core/lib/security/authorization/rbac_policy.h"

#include <utility>

#include "src/core/util/string.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

//
// Rbac
//

Rbac::Rbac(std::string name, Rbac::Action action,
           std::map<std::string, Policy> policies)
    : name(std::move(name)),
      action(action),
      policies(std::move(policies)),
      audit_condition(Rbac::AuditCondition::kNone) {}

Rbac::Rbac(Rbac&& other) noexcept
    : name(std::move(other.name)),
      action(other.action),
      policies(std::move(other.policies)),
      audit_condition(other.audit_condition),
      logger_configs(std::move(other.logger_configs)) {}

Rbac& Rbac::operator=(Rbac&& other) noexcept {
  name = std::move(other.name);
  action = other.action;
  policies = std::move(other.policies);
  audit_condition = other.audit_condition;
  logger_configs = std::move(other.logger_configs);
  return *this;
}

bool Rbac::operator==(const Rbac& other) const {
  if (name != other.name) return false;
  if (action != other.action) return false;
  if (policies != other.policies) return false;
  if (audit_condition != other.audit_condition) return false;
  if (logger_configs.size() != other.logger_configs.size()) return false;
  for (size_t i = 0; i < logger_configs.size(); ++i) {
    if (logger_configs[i] == nullptr) {
      if (other.logger_configs[i] != nullptr) return false;
      continue;
    }
    if (other.logger_configs[i] == nullptr) return false;
    if (logger_configs[i]->name() != other.logger_configs[i]->name() ||
        logger_configs[i]->ToString() != other.logger_configs[i]->ToString()) {
      return false;
    }
  }
  return true;
}

namespace {

absl::string_view AuditConditionString(Rbac::AuditCondition audit_condition) {
  switch (audit_condition) {
    case Rbac::AuditCondition::kNone:
      return "None";
    case Rbac::AuditCondition::kOnDeny:
      return "OnDeny";
    case Rbac::AuditCondition::kOnAllow:
      return "OnAllow";
    case Rbac::AuditCondition::kOnDenyAndAllow:
      return "OnDenyAndAllow";
  }
  return "<UNKNOWN>";
}

}  // namespace

std::string Rbac::ToString() const {
  std::string str = "Rbac{name=";
  StrAppend(str, name);
  StrAppend(str, ", action=");
  StrAppend(str, action == Rbac::Action::kAllow ? "Allow" : "Deny");
  StrAppend(str, ", audit_condition=");
  StrAppend(str, AuditConditionString(audit_condition));
  StrAppend(str, ", policies={");
  bool is_first = true;
  for (const auto& [name, policy] : policies) {
    if (!is_first) StrAppend(str, ", ");
    StrAppend(str, name);
    StrAppend(str, "=");
    StrAppend(str, policy.ToString());
    is_first = false;
  }
  StrAppend(str, "}, audit_loggers={");
  is_first = true;
  for (const auto& config : logger_configs) {
    if (!is_first) StrAppend(str, ", ");
    StrAppend(str, config->name());
    StrAppend(str, "=");
    StrAppend(str, config->ToString());
    is_first = false;
  }
  StrAppend(str, "}}");
  return str;
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

bool Rbac::CidrRange::operator==(const Rbac::CidrRange& other) const {
  return address_prefix == other.address_prefix &&
         prefix_len == other.prefix_len;
}

std::string Rbac::CidrRange::ToString() const {
  return absl::StrFormat("CidrRange{address_prefix=%s,prefix_len=%d}",
                         address_prefix, prefix_len);
}

//
// Permission
//

Rbac::Permission Rbac::Permission::MakeAndPermission(
    std::vector<std::unique_ptr<Permission>> permissions) {
  Permission permission;
  permission.type = Permission::RuleType::kAnd;
  permission.permissions = std::move(permissions);
  return permission;
}

Rbac::Permission Rbac::Permission::MakeOrPermission(
    std::vector<std::unique_ptr<Permission>> permissions) {
  Permission permission;
  permission.type = Permission::RuleType::kOr;
  permission.permissions = std::move(permissions);
  return permission;
}

Rbac::Permission Rbac::Permission::MakeNotPermission(
    std::unique_ptr<Permission> permission) {
  Permission not_permission;
  not_permission.type = Permission::RuleType::kNot;
  not_permission.permissions.push_back(std::move(permission));
  return not_permission;
}

Rbac::Permission Rbac::Permission::MakeAnyPermission() {
  Permission permission;
  permission.type = Permission::RuleType::kAny;
  return permission;
}

Rbac::Permission Rbac::Permission::MakeHeaderPermission(
    HeaderMatcher header_matcher) {
  Permission permission;
  permission.type = Permission::RuleType::kHeader;
  permission.header_matcher = std::move(header_matcher);
  return permission;
}

Rbac::Permission Rbac::Permission::MakePathPermission(
    StringMatcher string_matcher) {
  Permission permission;
  permission.type = Permission::RuleType::kPath;
  permission.string_matcher = std::move(string_matcher);
  return permission;
}

Rbac::Permission Rbac::Permission::MakeDestIpPermission(CidrRange ip) {
  Permission permission;
  permission.type = Permission::RuleType::kDestIp;
  permission.ip = std::move(ip);
  return permission;
}

Rbac::Permission Rbac::Permission::MakeDestPortPermission(int port) {
  Permission permission;
  permission.type = Permission::RuleType::kDestPort;
  permission.port = port;
  return permission;
}

Rbac::Permission Rbac::Permission::MakeMetadataPermission(bool invert) {
  Permission permission;
  permission.type = Permission::RuleType::kMetadata;
  permission.invert = invert;
  return permission;
}

Rbac::Permission Rbac::Permission::MakeReqServerNamePermission(
    StringMatcher string_matcher) {
  Permission permission;
  permission.type = Permission::RuleType::kReqServerName;
  permission.string_matcher = std::move(string_matcher);
  return permission;
}

Rbac::Permission::Permission(Rbac::Permission&& other) noexcept
    : type(other.type), invert(other.invert) {
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
  invert = other.invert;
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

bool Rbac::Permission::operator==(const Rbac::Permission& other) const {
  if (type != other.type) return false;
  if (header_matcher != other.header_matcher) return false;
  if (string_matcher != other.string_matcher) return false;
  if (ip != other.ip) return false;
  if (port != other.port) return false;
  if (invert != other.invert) return false;
  if (permissions.size() != other.permissions.size()) return false;
  for (size_t i = 0; i < permissions.size(); ++i) {
    if (permissions[i] == nullptr) {
      if (other.permissions[i] != nullptr) return false;
    } else {
      if (other.permissions[i] == nullptr) return false;
      if (*permissions[i] != *other.permissions[i]) return false;
    }
  }
  return true;
}

std::string Rbac::Permission::ToString() const {
  switch (type) {
    case RuleType::kAnd: {
      std::vector<std::string> contents;
      contents.reserve(permissions.size());
      for (const auto& permission : permissions) {
        contents.push_back(permission->ToString());
      }
      return absl::StrFormat("{and=[%s]}", absl::StrJoin(contents, ", "));
    }
    case RuleType::kOr: {
      std::vector<std::string> contents;
      contents.reserve(permissions.size());
      for (const auto& permission : permissions) {
        contents.push_back(permission->ToString());
      }
      return absl::StrFormat("{or=[%s]}", absl::StrJoin(contents, ", "));
    }
    case RuleType::kNot:
      return absl::StrFormat("{not %s}", permissions[0]->ToString());
    case RuleType::kAny:
      return "{any}";
    case RuleType::kHeader:
      return absl::StrFormat("{header=%s}", header_matcher.ToString());
    case RuleType::kPath:
      return absl::StrFormat("{path=%s}", string_matcher.ToString());
    case RuleType::kDestIp:
      return absl::StrFormat("{dest_ip=%s}", ip.ToString());
    case RuleType::kDestPort:
      return absl::StrFormat("{dest_port=%d}", port);
    case RuleType::kMetadata:
      return absl::StrFormat("{%smetadata}", invert ? "invert " : "");
    case RuleType::kReqServerName:
      return absl::StrFormat("{requested_server_name=%s}",
                             string_matcher.ToString());
    default:
      return "";
  }
}

//
// Principal
//

Rbac::Principal Rbac::Principal::MakeAndPrincipal(
    std::vector<std::unique_ptr<Principal>> principals) {
  Principal principal;
  principal.type = Principal::RuleType::kAnd;
  principal.principals = std::move(principals);
  return principal;
}

Rbac::Principal Rbac::Principal::MakeOrPrincipal(
    std::vector<std::unique_ptr<Principal>> principals) {
  Principal principal;
  principal.type = Principal::RuleType::kOr;
  principal.principals = std::move(principals);
  return principal;
}

Rbac::Principal Rbac::Principal::MakeNotPrincipal(
    std::unique_ptr<Principal> principal) {
  Principal not_principal;
  not_principal.type = Principal::RuleType::kNot;
  not_principal.principals.push_back(std::move(principal));
  return not_principal;
}

Rbac::Principal Rbac::Principal::MakeAnyPrincipal() {
  Principal principal;
  principal.type = Principal::RuleType::kAny;
  return principal;
}

Rbac::Principal Rbac::Principal::MakeAuthenticatedPrincipal(
    std::optional<StringMatcher> string_matcher) {
  Principal principal;
  principal.type = Principal::RuleType::kPrincipalName;
  principal.string_matcher = std::move(string_matcher);
  return principal;
}

Rbac::Principal Rbac::Principal::MakeSourceIpPrincipal(CidrRange ip) {
  Principal principal;
  principal.type = Principal::RuleType::kSourceIp;
  principal.ip = std::move(ip);
  return principal;
}

Rbac::Principal Rbac::Principal::MakeDirectRemoteIpPrincipal(CidrRange ip) {
  Principal principal;
  principal.type = Principal::RuleType::kDirectRemoteIp;
  principal.ip = std::move(ip);
  return principal;
}

Rbac::Principal Rbac::Principal::MakeRemoteIpPrincipal(CidrRange ip) {
  Principal principal;
  principal.type = Principal::RuleType::kRemoteIp;
  principal.ip = std::move(ip);
  return principal;
}

Rbac::Principal Rbac::Principal::MakeHeaderPrincipal(
    HeaderMatcher header_matcher) {
  Principal principal;
  principal.type = Principal::RuleType::kHeader;
  principal.header_matcher = std::move(header_matcher);
  return principal;
}

Rbac::Principal Rbac::Principal::MakePathPrincipal(
    StringMatcher string_matcher) {
  Principal principal;
  principal.type = Principal::RuleType::kPath;
  principal.string_matcher = std::move(string_matcher);
  return principal;
}

Rbac::Principal Rbac::Principal::MakeMetadataPrincipal(bool invert) {
  Principal principal;
  principal.type = Principal::RuleType::kMetadata;
  principal.invert = invert;
  return principal;
}

Rbac::Principal::Principal(Rbac::Principal&& other) noexcept
    : type(other.type), invert(other.invert) {
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
  invert = other.invert;
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

bool Rbac::Principal::operator==(const Rbac::Principal& other) const {
  if (type != other.type) return false;
  if (header_matcher != other.header_matcher) return false;
  if (string_matcher != other.string_matcher) return false;
  if (ip != other.ip) return false;
  if (invert != other.invert) return false;
  if (principals.size() != other.principals.size()) return false;
  for (size_t i = 0; i < principals.size(); ++i) {
    if (principals[i] == nullptr) {
      if (other.principals[i] != nullptr) return false;
    } else {
      if (other.principals[i] == nullptr) return false;
      if (*principals[i] != *other.principals[i]) return false;
    }
  }
  return true;
}

std::string Rbac::Principal::ToString() const {
  switch (type) {
    case RuleType::kAnd: {
      std::vector<std::string> contents;
      contents.reserve(principals.size());
      for (const auto& principal : principals) {
        contents.push_back(principal->ToString());
      }
      return absl::StrFormat("{and=[%s]}", absl::StrJoin(contents, ", "));
    }
    case RuleType::kOr: {
      std::vector<std::string> contents;
      contents.reserve(principals.size());
      for (const auto& principal : principals) {
        contents.push_back(principal->ToString());
      }
      return absl::StrFormat("{or=[%s]}", absl::StrJoin(contents, ", "));
    }
    case RuleType::kNot:
      return absl::StrFormat("{not %s}", principals[0]->ToString());
    case RuleType::kAny:
      return "{any}";
    case RuleType::kPrincipalName:
      return absl::StrFormat("{principal_name=%s}", string_matcher->ToString());
    case RuleType::kSourceIp:
      return absl::StrFormat("{source_ip=%s}", ip.ToString());
    case RuleType::kDirectRemoteIp:
      return absl::StrFormat("{direct_remote_ip=%s}", ip.ToString());
    case RuleType::kRemoteIp:
      return absl::StrFormat("{remote_ip=%s}", ip.ToString());
    case RuleType::kHeader:
      return absl::StrFormat("{header=%s}", header_matcher.ToString());
    case RuleType::kPath:
      return absl::StrFormat("{path=%s}", string_matcher->ToString());
    case RuleType::kMetadata:
      return absl::StrFormat("{%smetadata}", invert ? "invert " : "");
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

bool Rbac::Policy::operator==(const Rbac::Policy& other) const {
  return permissions == other.permissions && principals == other.principals;
}

std::string Rbac::Policy::ToString() const {
  std::string str = "{permissions=";
  StrAppend(str, permissions.ToString());
  StrAppend(str, ", principals=");
  StrAppend(str, principals.ToString());
  StrAppend(str, "}");
  return str;
}

}  // namespace grpc_core
