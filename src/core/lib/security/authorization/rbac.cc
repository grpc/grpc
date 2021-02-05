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

}  // namespace grpc_core
