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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MATCHERS_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MATCHERS_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/lib/matchers/matchers.h"
#include "src/core/lib/security/authorization/evaluate_args.h"
#include "src/core/lib/security/authorization/rbac_policy.h"

namespace grpc_core {

// Describes the rules for matching permission or principal.
class AuthorizationMatcher {
 public:
  virtual ~AuthorizationMatcher() = default;

  // Returns whether or not the permission/principal matches the rules of the
  // matcher.
  virtual bool Matches(const EvaluateArgs& args) const = 0;

  // Creates an instance of a matcher based off the rules defined in Permission
  // config.
  static std::unique_ptr<AuthorizationMatcher> Create(
      Rbac::Permission permission);

  // Creates an instance of a matcher based off the rules defined in Principal
  // config.
  static std::unique_ptr<AuthorizationMatcher> Create(
      Rbac::Principal principal);
};

class AlwaysAuthorizationMatcher : public AuthorizationMatcher {
 public:
  explicit AlwaysAuthorizationMatcher(bool not_rule = false)
      : not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs&) const override { return !not_rule_; }

 private:
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

class AndAuthorizationMatcher : public AuthorizationMatcher {
 public:
  explicit AndAuthorizationMatcher(
      std::vector<std::unique_ptr<Rbac::Permission>> rules,
      bool not_rule = false);
  explicit AndAuthorizationMatcher(
      std::vector<std::unique_ptr<Rbac::Principal>> ids, bool not_rule = false);

  bool Matches(const EvaluateArgs& args) const override;

 private:
  std::vector<std::unique_ptr<AuthorizationMatcher>> matchers_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

class OrAuthorizationMatcher : public AuthorizationMatcher {
 public:
  explicit OrAuthorizationMatcher(
      std::vector<std::unique_ptr<Rbac::Permission>> rules,
      bool not_rule = false);
  explicit OrAuthorizationMatcher(
      std::vector<std::unique_ptr<Rbac::Principal>> ids, bool not_rule = false);

  bool Matches(const EvaluateArgs& args) const override;

 private:
  std::vector<std::unique_ptr<AuthorizationMatcher>> matchers_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// TODO(ashithasantosh): Add matcher implementation for metadata field.

// Perform a match against HTTP headers.
class HeaderAuthorizationMatcher : public AuthorizationMatcher {
 public:
  explicit HeaderAuthorizationMatcher(HeaderMatcher matcher,
                                      bool not_rule = false)
      : matcher_(std::move(matcher)), not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs& args) const override;

 private:
  const HeaderMatcher matcher_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Perform a match against IP Cidr Range.
// TODO(ashithasantosh): Handle type of Ip or use seperate matchers for each
// type. Implement Match functionality, this would require updating EvaluateArgs
// getters, to return format of IP as well.
class IpAuthorizationMatcher : public AuthorizationMatcher {
 public:
  explicit IpAuthorizationMatcher(Rbac::CidrRange range, bool not_rule = false)
      : range_(std::move(range)), not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs&) const override;

 private:
  const Rbac::CidrRange range_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Perform a match against port number of the destination (local) address.
class PortAuthorizationMatcher : public AuthorizationMatcher {
 public:
  explicit PortAuthorizationMatcher(int port, bool not_rule = false)
      : port_(port), not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs& args) const override;

 private:
  const int port_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Matches the principal name as described in the peer certificate. Uses URI SAN
// or DNS SAN in that order, otherwise uses subject field.
class AuthenticatedAuthorizationMatcher : public AuthorizationMatcher {
 public:
  explicit AuthenticatedAuthorizationMatcher(StringMatcher auth,
                                             bool not_rule = false)
      : matcher_(std::move(auth)), not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs& args) const override;

 private:
  const StringMatcher matcher_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Perform a match against the request server from the client's connection
// request. This is typically TLS SNI. Currently unsupported.
class ReqServerNameAuthorizationMatcher : public AuthorizationMatcher {
 public:
  explicit ReqServerNameAuthorizationMatcher(
      StringMatcher requested_server_name, bool not_rule = false)
      : matcher_(std::move(requested_server_name)), not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs&) const override;

 private:
  const StringMatcher matcher_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Perform a match against the path header of HTTP request.
class PathAuthorizationMatcher : public AuthorizationMatcher {
 public:
  explicit PathAuthorizationMatcher(StringMatcher path, bool not_rule = false)
      : matcher_(std::move(path)), not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs& args) const override;

 private:
  const StringMatcher matcher_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Performs a match for policy field in RBAC, which is a collection of
// permission and principal matchers. Policy matches iff, we find a match in one
// of its permissions and a match in one of its principals.
class PolicyAuthorizationMatcher : public AuthorizationMatcher {
 public:
  explicit PolicyAuthorizationMatcher(Rbac::Policy policy)
      : permissions_(
            AuthorizationMatcher::Create(std::move(policy.permissions))),
        principals_(
            AuthorizationMatcher::Create(std::move(policy.principals))) {}

  bool Matches(const EvaluateArgs& args) const override;

 private:
  std::unique_ptr<AuthorizationMatcher> permissions_;
  std::unique_ptr<AuthorizationMatcher> principals_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MATCHERS_H
