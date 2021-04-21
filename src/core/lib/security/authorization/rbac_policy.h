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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_RBAC_POLICY_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_RBAC_POLICY_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/lib/matchers/matchers.h"

namespace grpc_core {

// Represents Envoy RBAC Proto. [See
// https://github.com/envoyproxy/envoy/blob/release/v1.17/api/envoy/config/rbac/v3/rbac.proto]
struct Rbac {
  enum class Action {
    kAllow,
    kDeny,
  };

  struct CidrRange {
    CidrRange() = default;
    CidrRange(std::string address_prefix, uint32_t prefix_len);

    CidrRange(CidrRange&& other) noexcept;
    CidrRange& operator=(CidrRange&& other) noexcept;

    std::string ToString() const;

    std::string address_prefix;
    uint32_t prefix_len;
  };

  // TODO(ashithasantosh): Add metadata field to Permission and Principal.
  struct Permission {
    enum class RuleType {
      kAnd,
      kOr,
      kAny,
      kHeader,
      kPath,
      kDestIp,
      kDestPort,
      kReqServerName,
    };

    Permission() = default;
    // For AND/OR RuleType.
    Permission(Permission::RuleType type,
               std::vector<std::unique_ptr<Permission>> permissions,
               bool not_rule = false);
    // For ANY RuleType.
    explicit Permission(Permission::RuleType type, bool not_rule = false);
    // For HEADER RuleType.
    Permission(Permission::RuleType type, HeaderMatcher header_matcher,
               bool not_rule = false);
    // For PATH/REQ_SERVER_NAME RuleType.
    Permission(Permission::RuleType type, StringMatcher string_matcher,
               bool not_rule = false);
    // For DEST_IP RuleType.
    Permission(Permission::RuleType type, CidrRange ip, bool not_rule = false);
    // For DEST_PORT RuleType.
    Permission(Permission::RuleType type, int port, bool not_rule = false);

    Permission(Permission&& other) noexcept;
    Permission& operator=(Permission&& other) noexcept;

    std::string ToString() const;

    RuleType type;
    HeaderMatcher header_matcher;
    StringMatcher string_matcher;
    CidrRange ip;
    int port;
    // For type AND/OR.
    std::vector<std::unique_ptr<Permission>> permissions;
    bool not_rule = false;
  };

  struct Principal {
    enum class RuleType {
      kAnd,
      kOr,
      kAny,
      kPrincipalName,
      kSourceIp,
      kDirectRemoteIp,
      kRemoteIp,
      kHeader,
      kPath,
    };

    Principal() = default;
    // For AND/OR RuleType.
    Principal(Principal::RuleType type,
              std::vector<std::unique_ptr<Principal>> principals,
              bool not_rule = false);
    // For ANY RuleType.
    explicit Principal(Principal::RuleType type, bool not_rule = false);
    // For PRINCIPAL_NAME/PATH RuleType.
    Principal(Principal::RuleType type, StringMatcher string_matcher,
              bool not_rule = false);
    // For SOURCE_IP/DIRECT_REMOTE_IP/REMOTE_IP RuleType.
    Principal(Principal::RuleType type, CidrRange ip, bool not_rule = false);
    // For HEADER RuleType.
    Principal(Principal::RuleType type, HeaderMatcher header_matcher,
              bool not_rule = false);

    Principal(Principal&& other) noexcept;
    Principal& operator=(Principal&& other) noexcept;

    std::string ToString() const;

    RuleType type;
    HeaderMatcher header_matcher;
    StringMatcher string_matcher;
    CidrRange ip;
    // For type AND/OR.
    std::vector<std::unique_ptr<Principal>> principals;
    bool not_rule = false;
  };

  struct Policy {
    Policy() = default;
    Policy(Permission permissions, Principal principals);

    Policy(Policy&& other) noexcept;
    Policy& operator=(Policy&& other) noexcept;

    std::string ToString() const;

    Permission permissions;
    Principal principals;
  };

  Rbac() = default;
  Rbac(Rbac::Action action, std::map<std::string, Policy> policies);

  Rbac(Rbac&& other) noexcept;
  Rbac& operator=(Rbac&& other) noexcept;

  std::string ToString() const;

  Action action;
  std::map<std::string, Policy> policies;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SECURITY_AUTHORIZATION_RBAC_POLICY_H */
