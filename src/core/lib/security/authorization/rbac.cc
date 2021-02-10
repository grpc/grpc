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

#include "src/core/lib/security/authorization/rbac.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

//
// Rbac
//

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
      "Rbac action=%s{", action == Rbac::Action::ALLOW ? "Allow" : "Deny"));
  for (auto it = policies.begin(); it != policies.end(); it++) {
    contents.push_back(absl::StrFormat("policy_name=%s\n%s", it->first,
                                       it->second.ToString()));
  }
  contents.push_back("}");
  return absl::StrJoin(contents, "\n");
}

//
// CidrRange
//

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

Rbac::Permission::Permission(Rbac::Permission&& other) noexcept
    : type(other.type), not_rule(other.not_rule) {
  switch (type) {
    case RuleType::AND:
    case RuleType::OR:
      permissions = std::move(other.permissions);
      break;
    case RuleType::ANY:
      break;
    case RuleType::HEADER:
      header_matcher = std::move(other.header_matcher);
      break;
    case RuleType::PATH:
    case RuleType::REQ_SERVER_NAME:
      string_matcher = std::move(other.string_matcher);
      break;
    case RuleType::DEST_IP:
      ip = std::move(other.ip);
      break;
    default:
      port = other.port;
  }
}

Rbac::Permission& Rbac::Permission::operator=(
    Rbac::Permission&& other) noexcept {
  type = other.type;
  not_rule = other.not_rule;
  switch (type) {
    case RuleType::AND:
    case RuleType::OR:
      permissions = std::move(other.permissions);
      break;
    case RuleType::ANY:
      break;
    case RuleType::HEADER:
      header_matcher = std::move(other.header_matcher);
      break;
    case RuleType::PATH:
    case RuleType::REQ_SERVER_NAME:
      string_matcher = std::move(other.string_matcher);
      break;
    case RuleType::DEST_IP:
      ip = std::move(other.ip);
      break;
    default:
      port = other.port;
  }
  return *this;
}

std::string Rbac::Permission::ToString() const {
  switch (type) {
    case RuleType::AND: {
      std::vector<std::string> contents;
      contents.reserve(permissions.size());
      for (int i = 0; i < permissions.size(); i++) {
        contents.push_back(absl::StrFormat("%s", permissions[i]->ToString()));
      }
      return absl::StrCat("and=[", absl::StrJoin(contents, ","), "]");
    }
    case RuleType::OR: {
      std::vector<std::string> contents;
      contents.reserve(permissions.size());
      for (int i = 0; i < permissions.size(); i++) {
        contents.push_back(absl::StrFormat("%s", permissions[i]->ToString()));
      }
      return absl::StrCat("or=[", absl::StrJoin(contents, ","), "]");
    }
    case RuleType::ANY:
      return "any";
    case RuleType::HEADER:
      return absl::StrFormat("header=%s", header_matcher.ToString());
    case RuleType::PATH:
      return absl::StrFormat("path=%s", string_matcher.ToString());
    case RuleType::DEST_IP:
      return absl::StrFormat("dest_ip=%s", ip.ToString());
    case RuleType::DEST_PORT:
      return absl::StrFormat("dest_port=%d", port);
    case RuleType::REQ_SERVER_NAME:
      return absl::StrFormat("requested_server_name=%s",
                             string_matcher.ToString());
    default:
      return "";
  }
}

//
// Principal
//

Rbac::Principal::Principal(Rbac::Principal&& other) noexcept
    : type(other.type), not_rule(other.not_rule) {
  switch (type) {
    case RuleType::AND:
    case RuleType::OR:
      principals = std::move(other.principals);
      break;
    case RuleType::ANY:
      break;
    case RuleType::HEADER:
      header_matcher = std::move(other.header_matcher);
      break;
    case RuleType::PRINCIPAL_NAME:
    case RuleType::PATH:
      string_matcher = std::move(other.string_matcher);
      break;
    default:
      ip = std::move(other.ip);
  }
}

Rbac::Principal& Rbac::Principal::operator=(Rbac::Principal&& other) noexcept {
  type = other.type;
  not_rule = other.not_rule;
  switch (type) {
    case RuleType::AND:
    case RuleType::OR:
      principals = std::move(other.principals);
      break;
    case RuleType::ANY:
      break;
    case RuleType::HEADER:
      header_matcher = std::move(other.header_matcher);
      break;
    case RuleType::PRINCIPAL_NAME:
    case RuleType::PATH:
      string_matcher = std::move(other.string_matcher);
      break;
    default:
      ip = std::move(other.ip);
  }
  return *this;
}

std::string Rbac::Principal::ToString() const {
  switch (type) {
    case RuleType::AND: {
      std::vector<std::string> contents;
      contents.reserve(principals.size());
      for (int i = 0; i < principals.size(); i++) {
        contents.push_back(absl::StrFormat("%s", principals[i]->ToString()));
      }
      return absl::StrCat("and=[", absl::StrJoin(contents, ","), "]");
    }
    case RuleType::OR: {
      std::vector<std::string> contents;
      contents.reserve(principals.size());
      for (int i = 0; i < principals.size(); i++) {
        contents.push_back(absl::StrFormat("%s", principals[i]->ToString()));
      }
      return absl::StrCat("or=[", absl::StrJoin(contents, ","), "]");
    }
    case RuleType::ANY:
      return "any";
    case RuleType::PRINCIPAL_NAME:
      return absl::StrFormat("principal_name=%s", string_matcher.ToString());
    case RuleType::SOURCE_IP:
      return absl::StrFormat("source_ip=%s", ip.ToString());
    case RuleType::HEADER:
      return absl::StrFormat("header=%s", header_matcher.ToString());
    case RuleType::PATH:
      return absl::StrFormat("path=%s", string_matcher.ToString());
    default:
      return "";
  }
}

//
// Policy
//

Rbac::Policy::Policy(Rbac::Policy&& other) noexcept
    : permissions(std::move(other.permissions)),
      principals(std::move(other.principals)) {}

Rbac::Policy& Rbac::Policy::operator=(Rbac::Policy&& other) noexcept {
  permissions = std::move(other.permissions);
  principals = std::move(other.principals);
  return *this;
}

std::string Rbac::Policy::ToString() const {
  return absl::StrFormat("Policy{\n  Permissions={%s}\n  Principals={%s}\n}",
                         permissions.ToString(), principals.ToString());
}

}  // namespace grpc_core
