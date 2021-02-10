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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_RBAC_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_RBAC_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/lib/security/authorization/matchers.h"

namespace grpc_core {

// Represents Envoy RBAC Proto. [See
// https://github.com/envoyproxy/envoy/blob/release/v1.17/api/envoy/config/rbac/v3/rbac.proto]
struct Rbac {
  struct CidrRange {
    CidrRange() = default;
    CidrRange(CidrRange&& other) noexcept;
    CidrRange& operator=(CidrRange&& other) noexcept;

    std::string ToString() const;

    std::string address_prefix;
    uint32_t prefix_len;
  };

  struct Permission {
    Permission() = default;
    Permission(Permission&& other) noexcept;
    Permission& operator=(Permission&& other) noexcept;

    std::string ToString() const;

    enum class RuleType {
      AND,
      OR,
      ANY,
      HEADER,
      PATH,
      DEST_IP,
      DEST_PORT,
      REQ_SERVER_NAME,
    };
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
    Principal() = default;
    Principal(Principal&& other) noexcept;
    Principal& operator=(Principal&& other) noexcept;

    std::string ToString() const;

    enum class RuleType {
      AND,
      OR,
      ANY,
      PRINCIPAL_NAME,
      SOURCE_IP,
      HEADER,
      PATH,
    };
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
    Policy(Policy&& other) noexcept;
    Policy& operator=(Policy&& other) noexcept;

    std::string ToString() const;

    Permission permissions;
    Principal principals;
  };

  Rbac() = default;
  Rbac(Rbac&& other) noexcept;
  Rbac& operator=(Rbac&& other) noexcept;

  std::string ToString() const;

  enum class Action {
    ALLOW,
    DENY,
  };
  Action action;
  std::map<std::string, Policy> policies;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SECURITY_AUTHORIZATION_RBAC_H */